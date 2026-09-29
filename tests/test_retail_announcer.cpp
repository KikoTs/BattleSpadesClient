#include "battlespades/frontend/retail_announcer.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::frontend;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

KillAnnouncementInput local_kill(std::uint8_t kill_count, bool domination, bool revenge) {
    KillAnnouncementInput input;
    input.local_is_killer = true;
    input.killer_known = true;
    input.kill_count = kill_count;
    input.domination = domination;
    input.revenge = revenge;
    input.victim_name = "Victim";
    input.killer_name = "Me";
    return input;
}

void multikill_banners_follow_kill_count() {
    expect(kill_action_announcements(local_kill(1U, false, false)).empty(),
           "a single kill raises no banner");
    const char* expected[]{"KILL2", "KILL3", "KILL4", "KILL5"};
    for (std::uint8_t count = 2U; count <= 5U; ++count) {
        const auto result = kill_action_announcements(local_kill(count, false, false));
        expect(result.size() == 1U && result[0].string_id == expected[count - 2U] &&
                   result[0].stinger.empty() && result[0].parameter.empty(),
               "kill_count 2..5 must map to KILL2..KILL5 with no stinger");
    }
    const auto many = kill_action_announcements(local_kill(9U, false, false));
    expect(many.size() == 1U && many[0].string_id == "KILLM" && many[0].parameter == "9",
           "kill_count above 5 must use KILLM formatted with the count");
    expect(format_announcement("{0} x Multi Kill", many[0].parameter) == "9 x Multi Kill",
           "KILLM must format {0}");
}

void killer_branch_orders_revenge_domination_multikill() {
    const auto result = kill_action_announcements(local_kill(3U, true, true));
    expect(result.size() == 3U, "revenge + domination + multikill must raise three banners");
    expect(result[0].string_id == "YOU_GOT_REVENGE" && result[0].stinger == "ks_revenge" &&
               result[0].parameter == "Victim",
           "revenge comes first and names the victim");
    expect(result[1].string_id == "YOU_ARE_DOMINATING" &&
               result[1].stinger == "ks_domination" && result[1].parameter == "Victim",
           "domination follows revenge");
    expect(result[2].string_id == "KILL3", "the multikill banner comes last");
}

void victim_branch_names_the_killer_and_skips_multikill() {
    KillAnnouncementInput input;
    input.local_is_victim = true;
    input.killer_known = true;
    input.kill_count = 4U;
    input.domination = true;
    input.revenge = true;
    input.victim_name = "Me";
    input.killer_name = "Nemesis";
    const auto result = kill_action_announcements(input);
    expect(result.size() == 2U, "the victim sees only relationship banners");
    expect(result[0].string_id == "THEY_GOT_REVENGE" && result[0].parameter == "Nemesis" &&
               result[0].stinger == "ks_revenge",
           "THEY_GOT_REVENGE names the killer");
    expect(result[1].string_id == "THEY_ARE_DOMINATING" && result[1].parameter == "Nemesis" &&
               result[1].stinger == "ks_domination",
           "THEY_ARE_DOMINATING names the killer");
}

void bystanders_suicides_and_unknown_killers_are_silent() {
    KillAnnouncementInput bystander = local_kill(5U, true, true);
    bystander.local_is_killer = false;
    expect(kill_action_announcements(bystander).empty(),
           "kills between two other players raise nothing locally");

    KillAnnouncementInput suicide = local_kill(2U, true, true);
    suicide.local_is_victim = true;
    suicide.killer_is_victim = true;
    const auto result = kill_action_announcements(suicide);
    expect(result.size() == 2U && result[0].string_id == "THEY_GOT_REVENGE",
           "a suicide falls through to the victim branch, never the multikill branch");

    KillAnnouncementInput unknown = local_kill(3U, true, false);
    unknown.killer_known = false;
    expect(kill_action_announcements(unknown).empty(), "retail guards everything on `if killer`");
}

void running_local_kills_track_the_death_cam_streak() {
    RunningLocalKills streaks;
    KillAnnouncementInput death;
    death.local_is_victim = true;
    death.killer_known = true;
    expect(streaks.apply(death, 1U, 7U) == 1U, "first death to a killer is streak 1");
    expect(streaks.apply(death, 1U, 7U) == 2U, "second death increments");
    KillAnnouncementInput kill = local_kill(1U, false, false);
    static_cast<void>(streaks.apply(kill, 7U, 1U));
    expect(streaks.count(7U) == 0U, "killing that player resets their streak");
    static_cast<void>(streaks.apply(death, 1U, 7U));
    streaks.forget(7U);
    expect(streaks.count(7U) == 0U, "PlayerLeft forgets the streak");
}

void match_start_stinger_matches_select_team() {
    const auto normal = match_start_stinger(false);
    expect(normal.stem == "mu_start_game" && normal.start_secondary_bed,
           "non-classic plays mu_start_game then the secondary bed");
    const auto classic = match_start_stinger(true);
    expect(classic.stem == "classic_mu_start_game" && !classic.start_secondary_bed,
           "classic plays its own stinger and no bed");

    ScreenEntryEdge edge;
    expect(!edge.update(false), "no entry while outside the menu");
    expect(edge.update(true), "entering the team menu fires once");
    expect(!edge.update(true), "staying in the menu does not refire");
    expect(!edge.update(false), "leaving does not fire");
    expect(edge.update(true), "re-entering (back from class select) fires again");
}

} // namespace

int main() {
    try {
        multikill_banners_follow_kill_count();
        killer_branch_orders_revenge_domination_multikill();
        victim_branch_names_the_killer_and_skips_multikill();
        bystanders_suicides_and_unknown_killers_are_silent();
        running_local_kills_track_the_death_cam_streak();
        match_start_stinger_matches_select_team();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
