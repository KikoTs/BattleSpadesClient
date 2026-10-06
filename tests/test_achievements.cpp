#include "battlespades/frontend/achievements.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using battlespades::frontend::AchievementLedger;
using battlespades::frontend::find_achievement;
using battlespades::frontend::find_achievement_by_display_name;
using battlespades::frontend::retail_achievement_definitions;

void expect(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void the_table_is_the_retail_schema() {
    const auto definitions = retail_achievement_definitions();
    expect(definitions.size() == 77U, "retail defines 77 achievements");
    std::set<std::string_view> names;
    std::set<std::string_view> displays;
    std::set<std::string_view> stats;
    for (const auto& definition : definitions) {
        expect(!definition.api_name.empty() && !definition.display_name.empty() &&
                   !definition.description.empty() && definition.token.starts_with("NEW_ACHIEVEMENT_"),
               "every achievement has its names, description and token");
        expect(definition.stat.empty() == (definition.threshold == 0U),
               "a counter-backed achievement has a threshold, a one-shot one has neither");
        names.insert(definition.api_name);
        displays.insert(definition.display_name);
        if (!definition.stat.empty()) stats.insert(definition.stat);
    }
    expect(names.size() == 77U && displays.size() == 77U, "API and display names are unique");
    expect(stats.size() == 32U, "35 achievements share 32 counters");
    expect(std::ranges::is_sorted(definitions, {}, &battlespades::frontend::AchievementDefinition::api_name),
           "the table is ordered by API name for lookup");
}

void lookups_find_what_the_server_announces() {
    const auto* spade = find_achievement("spade_kill");
    expect(spade != nullptr && spade->display_name == "Dig Deep" && spade->threshold == 10U &&
               spade->stat == "spade_kill_count",
           "an achievement is found by API name");
    expect(find_achievement("not_an_achievement") == nullptr, "unknown API names are rejected");
    // ACHIEVEMENT_GAINED carries the display name.
    const auto* named = find_achievement_by_display_name("hey, is that a sni-");
    expect(named != nullptr && named->api_name == "sniper_kill_hard",
           "the announced display name resolves, whatever its case");
    expect(find_achievement_by_display_name("Dig Deeper") == nullptr, "a near miss is not a match");
    // Two tiers of one counter stay distinct achievements.
    expect(find_achievement("misc_marathon")->threshold == 42000U &&
               find_achievement("misc_half_marathon")->threshold == 21000U,
           "tiers sharing a counter keep their own thresholds");
}

void the_ledger_remembers_unlocks() {
    const auto file = std::filesystem::temp_directory_path() / "aos_achievement_ledger_test.json";
    std::filesystem::remove(file);
    {
        AchievementLedger ledger{file};
        expect(ledger.entries().empty(), "a missing file is an empty ledger");
        expect(ledger.unlock("spade_kill", 1791000000), "a first unlock is recorded");
        expect(!ledger.unlock("spade_kill", 1791000999), "a repeat is not");
        expect(!ledger.unlock("made_up", 1), "only retail achievements are stored");
        expect(ledger.unlock("misc_five_in_a_row", 1791000100), "a second achievement is recorded");
        expect(ledger.save(), "the ledger saves");
    }
    {
        AchievementLedger reloaded{file};
        expect(reloaded.entries().size() == 2U && reloaded.unlocked("spade_kill") &&
                   reloaded.unlocked("misc_five_in_a_row") && !reloaded.unlocked("pickaxe_kill"),
               "unlocks survive a restart");
        expect(reloaded.entries().front().unlocked_at == 1791000000, "the first unlock time is kept");
    }
    {
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out << "{ this is not json";
    }
    expect(AchievementLedger{file}.entries().empty(), "a damaged file is an empty ledger, not an error");
    std::filesystem::remove(file);
}

}  // namespace

int main() {
    try {
        the_table_is_the_retail_schema();
        lookups_find_what_the_server_announces();
        the_ledger_remembers_unlocks();
        std::cout << "3/3 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
