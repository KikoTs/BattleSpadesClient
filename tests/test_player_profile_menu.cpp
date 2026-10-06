#include "battlespades/frontend/player_profile_menu.hpp"
#include "battlespades/frontend/player_profile_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
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

PlayerProfileData fixture() {
    PlayerProfileData result;
    result.player_name = "KikoTs";
    result.kill_death_ratio = 1.25;
    constexpr std::string_view summary_labels[]{
        "SOLDIER",
        "SCOUT",
        "ENGINEER2",
        "MINER",
        "GANGSTER",
        "SPECIALIST",
        "MEDIC",
        "TDM_TITLE",
        "CTF_TITLE",
        "DIAMOND_MINE_TITLE",
        "DEMOLITION_TITLE",
        "MULTIHILL_TITLE",
        "OCCUPATION_MODE_TITLE",
        "TC_TITLE",
        "VIP_MODE_TITLE",
        "ZOMBIE_MODE_TITLE",
        "CLASSIC",
    };
    for (const auto label : summary_labels) {
        result.rows[0].push_back(
            {PlayerProfileRowKind::summary_rank, "", std::string{label}, "Advanced", {}, {}});
    }
    result.rows[1] = {
        {PlayerProfileRowKind::category, "GENERAL", "GENERAL", "", {}, {}},
        {PlayerProfileRowKind::statistic,
         "GENERAL",
         "LEADERBOARD_KILLS",
         "20",
         0.5,
         PlayerProfileLevelDetails{3U, 20.0, 10.0, 30.0}},
        {PlayerProfileRowKind::category, "CTF_TITLE", "CTF_TITLE", "", {}, {}},
        {PlayerProfileRowKind::statistic, "CTF_TITLE", "LEADERBOARD_CAPTURE", "4", {}, {}},
    };
    result.rows[2] = {
        {PlayerProfileRowKind::category, "SOLDIER", "SOLDIER", "", {}, {}},
        {PlayerProfileRowKind::statistic, "SOLDIER", "LEADERBOARD_KILLS", "9", {}, {}},
    };
    result.rows[3] = {
        {PlayerProfileRowKind::category, "WEAPON_ACCURACY", "WEAPON_ACCURACY", "", {}, {}},
    };
    return result;
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

[[nodiscard]] std::size_t count_text(const battlespades::ui::DrawList& draw, std::string_view key) {
    return static_cast<std::size_t>(
        std::ranges::count_if(draw.commands(), [key](const auto& command) {
            const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command);
            return text != nullptr && text->localization_key == key;
        }));
}

void recovered_tabs_and_filters_keep_retail_order() {
    const auto tabs = player_profile_tab_definitions();
    expect(tabs.size() == 5U && tabs[0].label_key == "PLAYER_STATS" &&
               tabs[1].label_key == "GAME_MODES" && tabs[2].label_key == "CLASSES" &&
               tabs[3].label_key == "EQUIPMENT" && tabs[4].tab == PlayerProfileTab::inventory,
           "Inventory follows the four original mastery tabs");
    expect(tabs[1].filter_keys.size() == 11U && tabs[1].filter_keys[6] == "CTF_TITLE" &&
               tabs[1].filter_keys.back() == "HOURS_PLAYED",
           "Game Modes must retain every recovered retail filter in order");
    expect(tabs[2].filter_keys.size() == 9U && tabs[2].filter_keys[4] == "SPECIALIST" &&
               tabs[2].filter_keys.back() == "ZOMBIE",
           "Classes must retain all nine recovered class filters");
    expect(tabs[3].filter_keys.size() == 2U && tabs[3].filter_keys[0] == "WEAPON_ACCURACY" &&
               tabs[3].filter_keys[1] == "WEAPON_POINTS",
           "Equipment must retain accuracy and points filters");
}

void request_filter_reset_dropdown_and_effects_match_retail() {
    PlayerProfileMenuModel model{42U};
    const auto request = model.take_request();
    expect(request.has_value() && request->account_id == 42U, "profile request must carry account");
    expect(!model.take_request().has_value(), "a profile request may only be taken once");
    expect(model.complete(*request, fixture()), "valid profile must load");
    expect(model.selected_tab() == PlayerProfileTab::player_stats &&
               model.visible_row_capacity() == 12U,
           "retail callback must return to the 12-row summary tab");

    expect(model.select_tab(PlayerProfileTab::game_modes), "Game Modes tab should activate");
    expect(model.visible_row_capacity() == 13U && model.filter_visible(),
           "non-summary tabs use thirteen data rows and expose filters");
    expect(model.toggle_filter() && model.filter_open(), "filter title button must open its list");
    expect(model.select_filter(7U) && !model.filter_open(), "CTF selection must close the list");
    expect(model.displayed_rows().size() == 2U &&
               model.displayed_rows().front().category_key == "CTF_TITLE",
           "selected filter must retain only its category rows");

    expect(model.select_tab(PlayerProfileTab::classes), "Classes tab should activate");
    expect(model.select_filter(1U), "a class filter should activate");
    expect(model.select_tab(PlayerProfileTab::game_modes) && model.selected_filter() == 0U,
           "retail reconstructs each drop-down at All when a tab is entered");
    expect(model.displayed_rows().size() == fixture().rows[1].size(),
           "returning to Game Modes must restore the unfiltered list");

    // Steam's overlay cannot show what a player earns now, so the button
    // opens the menu's own list and leads back out of it.
    expect(model.toggle_filter() && model.filter_open(), "the drop-down opens on Game Modes");
    model.activate_achievements();
    expect(model.achievements_open() && !model.filter_open() &&
               model.first_visible_achievement() == 0U,
           "Achievements opens the list at its top and closes the drop-down");
    constexpr std::size_t total{77U};
    expect(!model.scroll_achievements(-1, total), "the list does not scroll above its first row");
    expect(model.scroll_achievements(3, total) && model.first_visible_achievement() == 3U,
           "the list scrolls by rows");
    expect(model.scroll_achievements(500, total) &&
               model.first_visible_achievement() ==
                   total - PlayerProfileMenuModel::achievement_visible_rows &&
               !model.scroll_achievements(1, total),
           "the list stops with its last row at the bottom");
    expect(!model.scroll_achievements(1, 4U) && model.first_visible_achievement() == 0U,
           "a list shorter than the panel does not scroll");
    model.activate_achievements();
    expect(!model.achievements_open(), "the same button returns to the statistics");
    model.activate_achievements();
    expect(model.select_tab(PlayerProfileTab::game_modes) && !model.achievements_open(),
           "a tab leads out of the list, even the tab that was behind it");
    model.activate_achievements();

    model.reload(99U);
    expect(!model.achievements_open(), "reopening the screen starts on the statistics");
    expect(model.selected_tab() == PlayerProfileTab::player_stats && !model.filter_open(),
           "opening/reloading the screen must restore its initial retail state");
    expect(!model.complete(*request, fixture()), "old-account callback must be rejected");
    const auto replacement = model.take_request();
    expect(replacement.has_value() && replacement->generation != request->generation,
           "reload must issue a new generation");
    expect(model.fail(*replacement) && model.state() == PlayerProfileLoadState::not_found,
           "endpoint failure must expose Profile Not Found state");
}

void presentation_uses_recovered_coordinates_fonts_and_rows() {
    const auto layout = player_profile_classic_layout();
    expect(layout.outer_frame == battlespades::ui::DrawRect{130.0, 28.0, 540.0, 543.0},
           "small frame must use truncated 0.6 retail geometry");
    expect(layout.content_frame == battlespades::ui::DrawRect{166.0, 146.0, 464.0, 308.0},
           "profile content texture must use its recovered integer center anchor");
    expect(layout.tab_strip == battlespades::ui::DrawRect{152.0, 100.0, 496.0, 33.0},
           "tab hit strip must match playerProfileMenu.on_mouse_press");
    expect(layout.cancel_button == battlespades::ui::DrawRect{152.0, 492.0, 240.0, 60.0} &&
               layout.achievements_button == battlespades::ui::DrawRect{405.0, 492.0, 240.0, 60.0},
           "TextButton.y=108 is the upper retail edge, placing controls at top-left y=492");

    PlayerProfileMenuModel model{1U};
    auto draw = PlayerProfilePresentation{}.build(model);
    const auto* loading = find_text(draw, "CONNECTING_PLEASE_WAIT");
    const auto* title = find_text(draw, "PLAYER_PROFILE");
    expect(loading != nullptr && title != nullptr && title->requested_font_size_pixels == 46.0,
           "loading state must draw retail copy and 46-pixel Spades title");
    const auto* frame = find_sprite(draw, player_profile_presentation_assets::outer_frame);
    expect(frame != nullptr && frame->destination == layout.outer_frame,
           "small frame command must preserve recovered bounds");

    const auto request = *model.take_request();
    expect(model.complete(request, fixture()), "fixture must load");
    draw = PlayerProfilePresentation{}.build(model);
    const auto* player_name = find_text(draw, "KikoTs");
    const auto* ratio = find_text(draw, "KILL_DEATH_RATIO: 1.250");
    const auto* summary_header = find_text(draw, "CLASS / MODE");
    const auto* first_rank = find_text(draw, "SOLDIER");
    expect(player_name != nullptr && player_name->requested_font_size_pixels == 26.0 &&
               player_name->destination == layout.player_name,
           "player name must use the recovered ammo/Spades font and position");
    expect(ratio != nullptr && ratio->requested_font_size_pixels == 11.0 &&
               ratio->destination == layout.kill_death_ratio,
           "summary K/D must be right aligned and formatted to three decimals");
    expect(summary_header != nullptr && first_rank != nullptr &&
               summary_header->destination.y == 185.0 && first_rank->destination.y == 208.0,
           "summary must synthesize its sticky red header and preserve the three-pixel gap");
    expect(find_sprite(draw, player_profile_presentation_assets::red_header_left) != nullptr,
           "category bars must use the recovered bevel-cap texture instead of a flat rectangle");
    expect(find_sprite(draw, player_profile_presentation_assets::arrow_up) != nullptr &&
               find_sprite(draw, player_profile_presentation_assets::scrollbar_mid) != nullptr,
           "retail list panel must retain its textured vertical scrollbar");
}

void achievements_list_replaces_the_statistics() {
    using battlespades::frontend::AchievementListRow;
    using battlespades::frontend::UnlockedAchievement;
    using battlespades::frontend::achievement_list;

    PlayerProfileMenuModel model{7U};
    const auto request = model.take_request();
    expect(request.has_value() && model.complete(*request, fixture()), "the profile loads");
    const std::vector<UnlockedAchievement> ledger{{"spade_kill", 1791244800}};
    const auto rows = achievement_list(ledger);
    PlayerProfilePresentationContext context;
    context.achievements = rows;

    auto closed = PlayerProfilePresentation{}.build(model, context);
    expect(find_text(closed, "Dig Deep") == nullptr && find_text(closed, "ACHIEVEMENTS") != nullptr,
           "rows are not drawn until the list is opened");

    model.activate_achievements();
    auto draw = PlayerProfilePresentation{}.build(model, context);
    const auto* name = find_text(draw, "Dig Deep");
    const auto* description = find_text(draw, "NEW_ACHIEVEMENT_2_30_DESC");
    expect(name != nullptr && description != nullptr,
           "an unlocked achievement leads the list with its name and retail description key");
    expect(name->destination.y == 187.0 && description->destination.y == 205.0 &&
               description->maximum_lines == 2U &&
               description->layout == battlespades::ui::TextLayout::bounded_wrapped_lines,
           "a row is a name line over a wrapped two-line description");
    expect(find_text(draw, "2026-10-06") != nullptr && find_text(draw, "1 / 77") != nullptr,
           "an unlock shows its date and the header counts them");
    expect(find_text(draw, "Apocalypse Later") != nullptr &&
               name->modulation.color != find_text(draw, "Apocalypse Later")->modulation.color,
           "locked achievements follow in retail's order, dimmed");
    expect(find_text(draw, "PLAYER_STATS") != nullptr && find_text(draw, "ACHIEVEMENTS") != nullptr &&
               find_text(draw, "CANCEL") != nullptr,
           "the button leads back to the statistics and the heading names the list");
    expect(find_text(draw, fixture().player_name) == nullptr &&
               find_sprite(draw, player_profile_presentation_assets::tab_active) == nullptr,
           "the statistics and the current-tab highlight give way to the list");
    std::size_t names{};
    for (const auto& row : rows) names += count_text(draw, row.definition->display_name);
    expect(names == PlayerProfileMenuModel::achievement_visible_rows, "six rows fill the panel");

    expect(model.scroll_achievements(500, rows.size()), "the list scrolls to its end");
    draw = PlayerProfilePresentation{}.build(model, context);
    expect(find_text(draw, "Dig Deep") == nullptr &&
               find_text(draw, rows.back().definition->display_name) != nullptr,
           "scrolling moves the window over the rows");

    // The list must not depend on the profile service being reachable.
    PlayerProfileMenuModel offline{7U};
    const auto failed = offline.take_request();
    expect(failed.has_value() && offline.fail(*failed), "the profile service is unreachable");
    offline.activate_achievements();
    draw = PlayerProfilePresentation{}.build(offline, context);
    expect(find_text(draw, "Dig Deep") != nullptr && find_text(draw, "PROFILE_NOT_FOUND") == nullptr,
           "achievements show without a profile");
}

void dropdown_and_progress_states_are_explicit() {
    PlayerProfileMenuModel model{5U};
    const auto request = *model.take_request();
    expect(model.complete(request, fixture()), "fixture must load");
    expect(model.select_tab(PlayerProfileTab::game_modes), "Game Modes tab should activate");
    expect(model.toggle_filter(), "drop-down should open");

    const auto draw = PlayerProfilePresentation{}.build(
        model,
        PlayerProfilePresentationContext{{800, 600},
                                         1'000U,
                                         PlayerProfilePresentationContext::ControlState::hovered,
                                         PlayerProfilePresentationContext::ControlState::pressed,
                                         PlayerProfilePresentationContext::ControlState::hovered});
    expect(count_text(draw, "ALL") == 2U,
           "open filter must draw All in both its title and option list");
    expect(find_text(draw, "HOURS_PLAYED") != nullptr,
           "open Game Modes filter must expose its final retail option");
    expect(find_sprite(draw, "png/ui/common_elements/buttons/button_large_press_left.png") !=
               nullptr,
           "pressed Achievements state must use the retail pressed button texture");
    expect(find_sprite(draw, player_profile_presentation_assets::square_button_hover) != nullptr,
           "hovered drop-down arrow must use the square-button hover texture");
    expect(find_text(draw, "LEVEL 3") != nullptr && find_text(draw, "20 / 30") != nullptr,
           "rank progress must show retail level and current/next values over the bar");
}

void expect_renderable(const PlayerProfileMenuModel& model) {
    const auto draw = PlayerProfilePresentation{}.build(model);
    expect(!draw.empty(), "Profile presentation must not be empty");
    for (const auto& command : draw.commands()) {
        if (const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command)) {
            const auto& rect = sprite->destination;
            if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
                !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
                rect.width <= 0.0 || rect.height <= 0.0) {
                throw std::runtime_error{
                    "Profile submitted a non-renderable sprite: " + sprite->asset_id + " (" +
                    std::to_string(rect.width) + " x " + std::to_string(rect.height) + ")"};
            }
        }
    }
}

void zero_and_boundary_progress_remain_renderable() {
    for (const auto tab : {PlayerProfileTab::game_modes, PlayerProfileTab::classes,
                           PlayerProfileTab::equipment}) {
        for (const auto fraction : {0.0, 0.5, 1.0}) {
            for (const auto detailed : {false, true}) {
                PlayerProfileMenuModel model{1U};
                PlayerProfileData data;
                data.player_name = "Progress fixture";
                PlayerProfileRow row{PlayerProfileRowKind::statistic, "", "LEADERBOARD_KILLS",
                                     "0", fraction, {}};
                if (detailed) {
                    // Zero fraction at a nonzero rank boundary must also omit the fill.
                    row.level_details = PlayerProfileLevelDetails{3U, 10.0 + 20.0 * fraction,
                                                                  10.0, 30.0};
                }
                data.rows[static_cast<std::size_t>(tab)].push_back(row);
                expect(model.complete(*model.take_request(), std::move(data)), "load progress fixture");
                static_cast<void>(model.select_tab(tab));
                expect_renderable(model);
                const auto draw = PlayerProfilePresentation{}.build(model);
                const auto fills = std::ranges::count_if(draw.commands(), [](const auto& command) {
                    const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
                    return sprite != nullptr && sprite->modulation.color ==
                        battlespades::ui::ColorRgba8{137U, 179U, 45U, 255U};
                });
                expect(fills == (fraction > 0.0 ? 1 : 0),
                       "empty progress must keep its background without drawing a fake fill");
            }
        }
        PlayerProfileMenuModel model{1U};
        PlayerProfileData data;
        data.player_name = "Zero threshold fixture";
        data.rows[static_cast<std::size_t>(tab)].push_back(
            {PlayerProfileRowKind::statistic, "", "LEADERBOARD_KILLS", "0", {},
             PlayerProfileLevelDetails{1U, 0.0, 0.0, 0.0}});
        expect(model.complete(*model.take_request(), std::move(data)), "load zero threshold fixture");
        static_cast<void>(model.select_tab(tab));
        expect_renderable(model);
    }
}

void long_stat_lists_keep_minimum_scrollbar_renderable() {
    for (const auto tab : {PlayerProfileTab::player_stats, PlayerProfileTab::game_modes,
                           PlayerProfileTab::classes, PlayerProfileTab::equipment}) {
        for (const auto count : {0U, 13U, 14U, 64U, 128U}) {
            PlayerProfileMenuModel model{1U};
            PlayerProfileData data;
            data.player_name = "Long profile fixture";
            const auto category = player_profile_tab_definition(tab).filter_keys.front();
            data.rows[static_cast<std::size_t>(tab)].assign(count,
                {PlayerProfileRowKind::statistic, std::string{category}, "LEADERBOARD_KILLS",
                 "9", {}, {}});
            expect(model.complete(*model.take_request(), std::move(data)), "load long profile fixture");
            static_cast<void>(model.select_tab(tab));
            expect_renderable(model);
            static_cast<void>(model.scroll_rows(1'000));
            expect_renderable(model);
            if (count == 128U) {
                const auto draw = PlayerProfilePresentation{}.build(model);
                expect(find_sprite(draw, player_profile_presentation_assets::scrollbar_top) != nullptr &&
                       find_sprite(draw, player_profile_presentation_assets::scrollbar_bottom) != nullptr,
                       "minimum thumb must retain both visible end caps");
                expect(find_sprite(draw, player_profile_presentation_assets::scrollbar_mid) == nullptr,
                       "minimum thumb must omit its zero-height middle");
            }
            if (model.filter_visible()) {
                static_cast<void>(model.select_filter(1U));
                expect_renderable(model);
                expect(model.toggle_filter(), "open filter on long profile");
                expect_renderable(model);
            }
        }
    }
}

void every_tab_and_filter_handles_loading_and_missing_profiles() {
    for (const auto& definition : player_profile_tab_definitions()) {
        PlayerProfileMenuModel model{1U};
        static_cast<void>(model.select_tab(definition.tab));
        expect_renderable(model);
        expect(model.fail(*model.take_request()), "set missing profile state");
        expect_renderable(model);
        if (!model.filter_visible()) continue;
        for (std::size_t filter = 0U; filter <= definition.filter_keys.size(); ++filter) {
            static_cast<void>(model.select_filter(filter));
            expect_renderable(model);
            expect(model.toggle_filter(), "open unavailable profile filter");
            expect_renderable(model);
            static_cast<void>(model.close_filter());
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
        {"zero_and_boundary_progress_remain_renderable", zero_and_boundary_progress_remain_renderable},
        {"long_stat_lists_keep_minimum_scrollbar_renderable", long_stat_lists_keep_minimum_scrollbar_renderable},
        {"every_tab_and_filter_handles_loading_and_missing_profiles", every_tab_and_filter_handles_loading_and_missing_profiles},
        {"recovered_tabs_and_filters_keep_retail_order",
         recovered_tabs_and_filters_keep_retail_order},
        {"request_filter_reset_dropdown_and_effects_match_retail",
         request_filter_reset_dropdown_and_effects_match_retail},
        {"presentation_uses_recovered_coordinates_fonts_and_rows",
         presentation_uses_recovered_coordinates_fonts_and_rows},
        {"dropdown_and_progress_states_are_explicit", dropdown_and_progress_states_are_explicit},
        {"achievements_list_replaces_the_statistics", achievements_list_replaces_the_statistics},
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
