#include "battlespades/frontend/class_loadout_store.hpp"
#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/frontend/class_selection_presentation.hpp"
#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/class_selection.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using battlespades::frontend::ClassLoadoutStore;
using battlespades::frontend::ClassSelectionMenuModel;
using battlespades::world::ClassSelectionRules;
using battlespades::world::SavedClassLoadout;

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

[[nodiscard]] bool contains(const std::vector<std::string>& values, std::string_view value) {
    return std::ranges::find(values, value) != values.end();
}

[[nodiscard]] bool contains(const std::vector<std::uint8_t>& values, unsigned value) {
    return std::ranges::find(values, static_cast<std::uint8_t>(value)) != values.end();
}

[[nodiscard]] bool has_sprite(const battlespades::ui::DrawList& draw, std::string_view asset) {
    return std::ranges::any_of(draw.commands(), [asset](const auto& command) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        return sprite != nullptr && sprite->asset_id == asset;
    });
}

/** A class with at least one loadout row offering two or more tools. */
struct RowChoice final {
    std::uint8_t class_id{};
    std::size_t group{};
    std::vector<std::uint16_t> options;
};

[[nodiscard]] RowChoice first_row_with_choices(std::uint8_t class_id) {
    const ClassSelectionRules rules;
    for (std::size_t group{}; group < 4U; ++group) {
        auto options = battlespades::world::class_row_options(class_id, group, rules);
        if (options.size() > 1U) return {class_id, group, std::move(options)};
    }
    throw std::runtime_error{"the class has no loadout row with a choice"};
}

void the_file_round_trips_with_retail_key_names() {
    ClassLoadoutStore::Loadouts loadouts;
    loadouts[0U] = SavedClassLoadout{{5U, 2U, 6U, 17U}, {"prefab_ultrabarrier", "flareblock"}};
    loadouts[12U] = SavedClassLoadout{{5U, 68U}, {}};
    const auto text = ClassLoadoutStore::serialize(loadouts);
    expect(text.find("\"loadout0\"") != std::string::npos &&
               text.find("\"prefabs0\"") != std::string::npos &&
               text.find("\"loadout12\"") != std::string::npos,
           "the file uses retail's loadout<N> / prefabs<N> config keys");
    expect(ClassLoadoutStore::parse(text) == loadouts, "a saved file reads back unchanged");
}

void damaged_files_fail_closed() {
    expect(ClassLoadoutStore::parse("").empty() && ClassLoadoutStore::parse("[1,2]").empty() &&
               ClassLoadoutStore::parse("{\"loadout0\": ").empty(),
           "text that is not one JSON object is an empty set");
    const auto parsed = ClassLoadoutStore::parse(
        R"({"loadout0": [5, 300], "loadout1": [5, "x"], "loadout2": [5, 7],
            "loadout99": [5], "loadoutx": [5], "prefabs2": ["../evil", "prefab_caltrop"],
            "prefabs3": ["prefab_caltrop", "prefab_caltrop", "London_Taxi"],
            "language": "en"})");
    expect(!parsed.contains(0U) && !parsed.contains(1U) && !parsed.contains(99U),
           "an entry with an out-of-range tool, a non-number or a bad class id is dropped");
    expect(parsed.contains(2U) && parsed.at(2U).loadout == std::vector<std::uint8_t>{5U, 7U} &&
               parsed.at(2U).prefabs.empty(),
           "a construct list holding a path is dropped as a whole, the tools are kept");
    expect(parsed.contains(3U) &&
               parsed.at(3U).prefabs ==
                   std::vector<std::string>{"prefab_caltrop", "London_Taxi"},
           "duplicate constructs collapse and plain names are kept as written");
}

void a_visit_merges_like_create_loadout_list() {
    ClassLoadoutStore::Loadouts saved;
    saved[0U] = SavedClassLoadout{{5U, 2U}, {"prefab_ultrabarrier"}};
    ClassLoadoutStore::Loadouts visit;
    visit[0U] = SavedClassLoadout{{5U, 3U}, {}};
    visit[1U] = SavedClassLoadout{{5U, 18U}, {"prefab_supertower"}};
    expect(ClassLoadoutStore::merge(saved, visit), "a new choice changes the saved set");
    expect(saved.at(0U).loadout == std::vector<std::uint8_t>{5U, 3U} &&
               saved.at(0U).prefabs == std::vector<std::string>{"prefab_ultrabarrier"},
           "the tools are replaced; an empty construct list leaves the saved constructs");
    expect(saved.at(1U) == visit.at(1U), "a class saved for the first time is added");
    expect(!ClassLoadoutStore::merge(saved, visit), "the same visit again changes nothing");
}

void the_store_writes_and_reads_a_real_file() {
    const auto directory =
        std::filesystem::temp_directory_path() / "battlespades-class-loadout-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    const ClassLoadoutStore store{directory / "nested" / "class_loadouts.json"};
    expect(store.load().empty(), "a missing file is an empty set");
    ClassLoadoutStore::Loadouts loadouts;
    loadouts[3U] = SavedClassLoadout{{5U, 0U, 21U}, {"prefab_superdome"}};
    expect(store.save(loadouts), "saving creates the directory and the file");
    expect(store.load() == loadouts, "the saved file loads back");
    loadouts[3U].loadout.push_back(59U);
    expect(store.save(loadouts) && store.load() == loadouts, "a second save replaces the file");
    {
        std::ofstream damaged{store.path(), std::ios::trunc};
        damaged << "not json";
    }
    expect(store.load().empty(), "a damaged file is an empty set, not an error");
    std::filesystem::remove_all(directory, error);
}

void saved_rows_open_on_the_saved_tool() {
    const auto choice = first_row_with_choices(0U);
    ClassSelectionRules rules;
    expect(battlespades::world::saved_row_indices(0U, rules) ==
               std::array<std::size_t, 4U>{},
           "without a saved loadout every row opens on its first tool");

    const auto saved_tool = static_cast<std::uint8_t>(choice.options[1U]);
    rules.saved[0U] = SavedClassLoadout{{5U, saved_tool}, {}};
    auto indices = battlespades::world::saved_row_indices(0U, rules);
    expect(indices[choice.group] == 1U, "the row opens on the saved tool");

    const auto automatic = battlespades::world::automatic_class_selection(0U, rules);
    expect(contains(automatic.loadout, saved_tool) &&
               !contains(automatic.loadout, choice.options[0U]),
           "GameClass(config) builds the automatic loadout from the saved row too");

    // get_valid_items skips disabled tools: the saved one is gone, so the row
    // falls back to the first tool the server still allows.
    rules.disabled_tools = {saved_tool};
    indices = battlespades::world::saved_row_indices(0U, rules);
    expect(indices[choice.group] == 0U, "a saved tool the server disabled is not selected");
    const auto options = battlespades::world::class_row_options(0U, choice.group, rules);
    expect(std::ranges::find(options, saved_tool) == options.end(),
           "the disabled tool is not offered either");

    // A saved tool of another class's row never leaks in.
    rules.disabled_tools.clear();
    rules.saved[0U] = SavedClassLoadout{{5U, 250U}, {}};
    expect(battlespades::world::saved_row_indices(0U, rules) ==
               std::array<std::size_t, 4U>{},
           "a saved tool the class does not offer selects nothing");
}

void saved_constructs_are_validated_against_the_class() {
    ClassSelectionRules rules;
    const auto stock = battlespades::world::default_class_constructs(12U, rules);
    expect(stock.size() == 3U, "the Engineer starts with its first three constructs");
    const auto offered = battlespades::world::class_construct_options(12U, rules);
    expect(offered.size() >= 5U, "the Engineer offers the flare tile and its constructs");

    rules.saved[12U] =
        SavedClassLoadout{{}, {offered.back(), "prefab_not_a_construct", "flareblock"}};
    const auto restored = battlespades::world::default_class_constructs(12U, rules);
    expect(restored == std::vector<std::string>{offered.back(), "flareblock"},
           "only saved constructs the class offers are restored, in saved order");

    rules.saved[12U] = SavedClassLoadout{{}, {"prefab_not_a_construct"}};
    expect(battlespades::world::default_class_constructs(12U, rules) == stock,
           "when no saved construct is offered any more the stock three return");

    rules.saved[12U] = SavedClassLoadout{{}, {offered.back()}};
    rules.disabled_tools = {battlespades::world::prefab_tool};
    expect(battlespades::world::default_class_constructs(12U, rules).empty(),
           "a server without the prefab tool gets no constructs");
    rules.disabled_tools.clear();
    rules.mafia = true;
    expect(battlespades::world::default_class_constructs(12U, rules).empty(),
           "mafia mode starts without constructs");
}

void map_prefabs_join_the_constructs_table() {
    ClassSelectionRules rules;
    const auto stock = battlespades::world::class_construct_options(12U, rules);
    rules.map_prefabs = {"prefab_london_taxi", "PREFAB_CALTROP", "../kv6/evil", "",
                         "prefab_london_taxi", "lunarsteps"};
    const auto offered = battlespades::world::class_construct_options(12U, rules);

    expect(offered.front() == battlespades::world::flare_block_construct,
           "the flare tile stays first");
    expect(!contains(offered, "prefab_caltrop") && contains(offered, "PREFAB_CALTROP"),
           "a class construct that is also a map prefab is listed once, by the map");
    expect(contains(offered, "prefab_london_taxi") && contains(offered, "lunarsteps") &&
               std::ranges::count(offered, std::string{"prefab_london_taxi"}) == 1,
           "the map's prefabs are offered once each");
    expect(!contains(offered, "../kv6/evil") && !contains(offered, ""),
           "a name that is not a plain prefab stem is never offered");
    expect(offered.size() == stock.size() + 2U,
           "two new names and one moved name: the table grows by two");

    const auto taxi = std::ranges::find(offered, std::string{"prefab_london_taxi"});
    const auto last_class = std::ranges::find(offered, stock.back());
    expect(last_class != offered.end() && last_class < taxi,
           "map prefabs follow the class constructs (CLASS_PREFABS order)");

    // CLASS_ZOMBIE lists CLASS_PREFABS_ZOMBIE alone: no MAP_PREFABS entry.
    expect(!battlespades::world::class_offers_map_prefabs(4U) &&
               !battlespades::world::class_offers_map_prefabs(5U) &&
               battlespades::world::class_offers_map_prefabs(12U),
           "zombies and the classic soldier do not list MAP_PREFABS");
    const auto zombie = battlespades::world::class_construct_options(4U, rules);
    expect(!contains(zombie, "prefab_london_taxi"), "a zombie is not offered map prefabs");

    const std::array<std::uint16_t, 0U> no_rows{};
    const std::vector<std::string> wanted{"prefab_london_taxi", "lunarsteps", "../kv6/evil"};
    const auto selection =
        battlespades::world::make_class_selection(12U, no_rows, wanted, rules);
    expect(selection.prefabs ==
               std::vector<std::string>{"prefab_london_taxi", "lunarsteps"},
           "SetClassLoadout carries the chosen map prefabs by the map's own names");

    rules.map_prefabs.clear();
    const auto without =
        battlespades::world::make_class_selection(12U, no_rows, wanted, rules);
    expect(without.prefabs.empty(),
           "a map prefab of another map is not sent to a server that does not list it");
}

void the_menu_restores_and_reports_saved_loadouts() {
    const auto soldier = first_row_with_choices(0U);
    const auto engineer = first_row_with_choices(12U);
    constexpr std::array<std::uint8_t, 2U> advertised{0U, 12U};

    ClassSelectionRules rules;
    rules.map_prefabs = {"prefab_london_taxi"};
    rules.saved[12U] = SavedClassLoadout{
        {5U, static_cast<std::uint8_t>(engineer.options[1U])}, {"prefab_london_taxi"}};

    ClassSelectionMenuModel menu;
    menu.configure(advertised, 2U, 0U, rules);
    expect(menu.option_indices() == std::array<std::size_t, 4U>{},
           "a class without a saved loadout opens on the first tools");
    expect(menu.session_loadouts(false).empty(),
           "nothing is written before the player switches class or confirms");

    menu.cycle_group(soldier.group, 1);
    menu.cycle_class(1);
    expect(menu.selected_class() == 12U &&
               menu.option_indices()[engineer.group] == 1U,
           "switching to a class with a saved loadout opens its rows on the saved tools");
    expect(std::vector<std::string>(menu.selected_prefabs().begin(),
                                    menu.selected_prefabs().end()) ==
               std::vector<std::string>{"prefab_london_taxi"},
           "the saved construct is selected, here the map's prefab");
    expect(contains(std::vector<std::string>(menu.construct_options().begin(),
                                             menu.construct_options().end()),
                    "prefab_london_taxi"),
           "the map's prefab is in the constructs table");

    auto visit = menu.session_loadouts(false);
    expect(visit.size() == 1U && visit.contains(0U) &&
               contains(visit.at(0U).loadout, soldier.options[1U]),
           "leaving a class card saves that class, as on_class_selected does");

    visit = menu.session_loadouts(true);
    expect(visit.size() == 2U && visit.contains(12U) &&
               contains(visit.at(12U).loadout, engineer.options[1U]) &&
               visit.at(12U).prefabs == std::vector<std::string>{"prefab_london_taxi"},
           "SELECT saves the shown class with its tools and constructs");

    const auto selection = menu.selection();
    expect(selection.class_id == 12U &&
               selection.prefabs == std::vector<std::string>{"prefab_london_taxi"} &&
               contains(selection.loadout, engineer.options[1U]),
           "the submitted SetClassLoadout matches the restored choice");

    // The next visit, on a server that disabled the saved tool.
    ClassLoadoutStore::Loadouts saved;
    static_cast<void>(ClassLoadoutStore::merge(saved, visit));
    ClassSelectionRules next;
    next.saved = saved;
    next.disabled_tools = {static_cast<std::uint8_t>(engineer.options[1U])};
    menu.configure(advertised, 2U, 12U, next);
    expect(menu.selected_class() == 12U && menu.option_indices()[engineer.group] == 0U,
           "a disabled saved tool falls back to the first allowed one");
    expect(!contains(menu.selection().prefabs, "prefab_london_taxi") &&
               menu.selection().prefabs.size() == 3U,
           "a saved map prefab of another map is replaced by the stock constructs");
}

void spectator_defaults_do_not_replace_the_saved_playing_loadout() {
    // Live join regression: a newly admitted spectator has default class 0
    // and minigun 8, while its saved Commando choice is assault rifle 60.
    auto saved = ClassLoadoutStore::parse(
        R"({"loadout0":[5,2,60,12,11,23,30,25,26],
            "prefabs0":["prefab_ultrabarrier"]})");
    ClassSelectionRules rules;
    rules.saved = saved;
    rules.disabled_tools = {22U};
    constexpr std::array<std::uint8_t, 2U> advertised{0U, 12U};
    const SavedClassLoadout spectator_defaults{
        {5U, 2U, 8U, 12U, 11U, 23U, 30U, 25U, 26U},
        battlespades::world::default_class_selection(0U).prefabs};
    ClassSelectionMenuModel menu;
    menu.configure(advertised, 2U, 0U, rules, false, true);
    expect(contains(menu.selection().loadout, 60U),
           "opening class selection must first read the saved assault rifle");
    menu.restore_playing_loadout(0U, 0U, spectator_defaults.loadout, spectator_defaults.prefabs);
    expect(contains(menu.selection().loadout, 60U) &&
               !contains(menu.selection().loadout, 8U) &&
               menu.selection().prefabs == std::vector<std::string>{"prefab_ultrabarrier"},
           "a spectator replica must not overwrite saved playing weapons or constructs");
    static_cast<void>(ClassLoadoutStore::merge(saved, menu.session_loadouts(true)));
    const auto reopened = ClassLoadoutStore::parse(ClassLoadoutStore::serialize(saved));
    expect(contains(reopened.at(0U).loadout, 60U) &&
               !contains(reopened.at(0U).loadout, 8U),
           "confirming spectator rejoin must preserve the saved weapon for the next visit");

    for (const auto absent_team : {1U, 255U}) {
        menu.restore_playing_loadout(static_cast<std::uint8_t>(absent_team), 0U,
                                     spectator_defaults.loadout, spectator_defaults.prefabs);
        expect(contains(menu.selection().loadout, 60U),
               "an unassigned replica must not replace the saved weapon either");
    }
    menu.restore_playing_loadout(2U, 12U, spectator_defaults.loadout, spectator_defaults.prefabs);
    expect(contains(menu.selection().loadout, 60U),
           "another current class must not restore overlapping tools into this class");
    for (const auto playing_team : {2U, 3U}) {
        menu.configure(advertised, 2U, 0U, rules, false, true);
        menu.restore_playing_loadout(static_cast<std::uint8_t>(playing_team), 0U,
                                     spectator_defaults.loadout, spectator_defaults.prefabs);
        expect(contains(menu.selection().loadout, 8U),
               "reopening a matching active class must still restore its current minigun");
        expect(!contains(menu.selection().loadout, 60U),
               "restoring an active primary weapon must replace the saved primary choice");
        expect(menu.selection().prefabs == spectator_defaults.prefabs,
               "reopening a matching active class must still restore its constructs");
    }
}

void server_loadout_overrides_replace_rows_without_leaking_between_matches() {
    using battlespades::world::class_row_options;
    ClassSelectionRules rules;
    rules.saved[0U].loadout = {8U};
    rules.loadout_overrides[{std::uint8_t{0U}, std::uint8_t{1U}}] = {61U, 60U};
    rules.loadout_overrides[{std::uint8_t{0U}, std::uint8_t{0U}}] = {};
    rules.loadout_overrides[{std::uint8_t{0U}, std::uint8_t{5U}}] = {5U, 30U};
    expect(class_row_options(0U, 1U, rules) == std::vector<std::uint16_t>{61U, 60U},
           "server replacement order includes weapons outside the stock class row");
    expect(class_row_options(0U, 0U, rules).empty(),
           "an explicitly empty server replacement removes the row");
    constexpr std::array<std::uint8_t, 1U> advertised{0U};
    ClassSelectionMenuModel menu;
    menu.configure(advertised, 2U, 0U, rules);
    const auto selection = menu.selection();
    expect(contains(selection.loadout, 61U) && !contains(selection.loadout, 8U),
           "a saved weapon absent from the replacement falls back to its first item");
    expect(contains(selection.loadout, 5U) && contains(selection.loadout, 30U) &&
               !contains(selection.loadout, 25U),
           "common tools also follow the server replacement");
    rules.disabled_tools = {61U};
    expect(class_row_options(0U, 1U, rules) == std::vector<std::uint16_t>{60U},
           "disabled tools remain excluded from server replacements");
    menu.configure(advertised, 2U, 0U, {});
    expect(std::ranges::equal(menu.row_options(1U), std::vector<std::uint16_t>{8U, 60U}),
           "joining another server restores stock rows without global table mutation");
}

void map_prefab_icons_resolve_like_get_prefab_image() {
    ClassSelectionRules rules;
    rules.map_prefabs = {"London_Taxi"};
    constexpr std::array<std::uint8_t, 1U> advertised{12U};
    ClassSelectionMenuModel menu;
    menu.configure(advertised, 2U, 12U, rules);
    const battlespades::frontend::ClassSelectionPresentation presentation;
    const auto draw = presentation.build(menu, battlespades::ui::PixelExtent{800, 600});
    expect(has_sprite(draw, "prefabs/prefab_london_taxi.png"),
           "a map prefab named without the prefix and in mixed case shows its icon");
    expect(has_sprite(draw, "prefabs/prefab_caltrop.png"),
           "class constructs keep their icons");
}

} // namespace

int main() {
    try {
        the_file_round_trips_with_retail_key_names();
        damaged_files_fail_closed();
        a_visit_merges_like_create_loadout_list();
        the_store_writes_and_reads_a_real_file();
        saved_rows_open_on_the_saved_tool();
        saved_constructs_are_validated_against_the_class();
        map_prefabs_join_the_constructs_table();
        the_menu_restores_and_reports_saved_loadouts();
        spectator_defaults_do_not_replace_the_saved_playing_loadout();
        server_loadout_overrides_replace_rows_without_leaking_between_matches();
        map_prefab_icons_resolve_like_get_prefab_image();
    } catch (const std::exception& error) {
        std::cerr << "class loadout store test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
