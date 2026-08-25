#include "battlespades/frontend/player_progression.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::frontend;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

void curve_preserves_retail_boundary_semantics() {
    const auto at_boundary = calculate_retail_progression_level(20.0);
    expect(at_boundary.level == 1U &&
               std::abs(at_boundary.percentage - 100.0) < 0.0001,
           "retail remains level one exactly at its first threshold");
    const auto after_boundary = calculate_retail_progression_level(21.0);
    expect(after_boundary.level == 2U && after_boundary.next_level_min == 20.0 &&
               after_boundary.next_level_max == 63.0,
           "retail promotes only when range(score) reaches the threshold");
    const auto malformed = calculate_retail_progression_level(-100.0, -1.0, 0.0);
    expect(malformed.level == 1U && malformed.next_level_min == 0.0,
           "untrusted negative profile values must fail safely to zero");
}

void recovered_catalog_builds_zero_rows_and_real_bars() {
    expect(retail_profile_stat_definition_count() == 182U &&
               retail_rank_definition_count() == 17U,
           "all recovered retail stat and rank definitions must be present");
    std::map<std::uint32_t, RetailProfileScoreValue> stats;
    stats[1U] = {21.0, 210.0};
    stats[220U] = {7.0, 0.0};
    stats[166U] = {90.0, 0.0};
    stats[1'006U] = {4.0, 0.0};
    stats[2'006U] = {3.0, 0.0};
    stats[3'006U] = {0.0, 125.0};
    const auto profile = build_retail_player_profile("KikoTs", stats);
    expect(profile.rows[0U].size() == 17U &&
               std::abs(profile.kill_death_ratio - 3.0) < 0.0001,
           "summary must retain seventeen independent class/mode ranks and K/D");
    const auto kill = std::ranges::find(profile.rows[1U], "TDM_Kill",
                                        &PlayerProfileRow::label_key);
    expect(kill != profile.rows[1U].end() && kill->value == "21" &&
               kill->level_details.has_value() && kill->level_details->level == 1U,
           "profile bar must use the count and exact retail level boundary");
    const auto london = std::ranges::find(profile.rows[1U], "London_time_score",
                                          &PlayerProfileRow::label_key);
    expect(london != profile.rows[1U].end() && london->value == "1.50",
           "map minutes must retain retail's two-decimal hours conversion");
    const auto rifle = std::ranges::find(profile.rows[3U], "RIFLE",
                                         &PlayerProfileRow::label_key);
    expect(rifle != profile.rows[3U].end() && rifle->value == "75%",
           "equipment accuracy must use recovered shot/hit stat namespaces");
    expect(retail_score_reason_label(250U) == "SPECIALIST_AutoPistol_Kills" &&
               retail_score_reason_label(238U) == "STAT_238",
           "duplicate retail ordinal must resolve exactly as Python tuple assignment");
}

} // namespace

int main() {
    try {
        curve_preserves_retail_boundary_semantics();
        recovered_catalog_builds_zero_rows_and_real_bars();
        std::cout << "retail player progression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "retail player progression tests failed: " << error.what() << '\n';
        return 1;
    }
}
