#include "battlespades/network/aosplay_scores.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

void presets_and_forms_match_the_published_contract() {
    using namespace battlespades::network;
    const auto general = aosplay_leaderboard_preset(0U);
    expect(general.has_value() && general->sort_stat == 201U &&
               general->stat_ids.size() == 5U && general->stat_ids[1U] == 220U,
           "general preset must match the published AoSPlay contract");
    const auto friends = build_aosplay_leaderboard_form(
        6U, AosPlayLeaderboardScope::friends, 1000000004U, 25U);
    expect(friends.find("sortid=197") != std::string::npos &&
               friends.find("noof_stats=6") != std::string::npos &&
               friends.find("noof_friends=1&friend=1000000004") != std::string::npos,
           "friend form must retain CTF stats and durable account identity");
    const std::array<std::uint64_t, 5U> social_friends{
        1000000001U, 1000000002U, 1000000001U, 0U, 1000000004U};
    const auto full_friends = build_aosplay_leaderboard_form(
        6U, AosPlayLeaderboardScope::friends, 1000000004U, 25U, social_friends);
    expect(full_friends.find(
               "noof_friends=3&friend=1000000001&friend=1000000002&friend=1000000004") !=
               std::string::npos,
           "friend form must de-duplicate social accounts and append the local player");
    expect(build_aosplay_leaderboard_form(
               10U, AosPlayLeaderboardScope::global, 0U, 25U)
               .empty(),
           "unknown leaderboard types must fail closed");
}

void leaderboard_parser_accepts_the_legacy_row_shape() {
    using namespace battlespades::network;
    constexpr std::string_view fixture{
        R"({"leaderboard":[[1000000004,"KikoTs",[12,345],{"1":[7,70],"159":[3,30]},4]]})"};
    const auto parsed = parse_aosplay_leaderboard(fixture);
    expect(parsed && parsed.rows.size() == 1U, "valid leaderboard fixture must parse");
    const auto& row = parsed.rows.front();
    expect(row.account_id == 1000000004U && row.player_name == "KikoTs" &&
               row.total.score == 345.0 && row.stats.at(1U).count == 7.0 &&
               row.global_rank == 4U,
           "all legacy leaderboard fields must retain their meaning");
}

void malformed_score_data_is_rejected_atomically() {
    using namespace battlespades::network;
    const auto bad_pair = parse_aosplay_leaderboard(
        R"({"leaderboard":[[1,"Player",[0,0],{"1":[0]},1]]})");
    expect(!bad_pair && bad_pair.rows.empty(),
           "a malformed score pair must reject the complete response");
    const auto oversized = parse_aosplay_leaderboard(
        R"({"leaderboard":[[1,"Player",[0,0],{},1],[2,"Other",[0,0],{},2]]})",
        1U);
    expect(oversized && oversized.rows.size() == 1U,
           "the parser must enforce its row cap without over-allocation");
}

void profile_parser_distinguishes_missing_from_malformed() {
    using namespace battlespades::network;
    const auto missing = parse_aosplay_profile("{}");
    expect(missing && !missing.profile.has_value(),
           "empty object is the service's valid profile-not-found response");
    const auto good = parse_aosplay_profile(
        R"({"profile":{"name":"KikoTs","total":[4,90],"stats":{"1":[3,30],"220":[2,0]}}})");
    expect(good && good.profile.has_value() &&
               good.profile->stats.at(220U).count == 2.0,
           "profile score map must parse by numeric stat ID");
    const auto malformed = parse_aosplay_profile(
        R"({"profile":{"name":"KikoTs","total":[4,90]}})");
    expect(!malformed && !malformed.profile.has_value(),
           "missing profile stats must be an adapter error");
}

} // namespace

int main() {
    try {
        presets_and_forms_match_the_published_contract();
        leaderboard_parser_accepts_the_legacy_row_shape();
        malformed_score_data_is_rejected_atomically();
        profile_parser_distinguishes_missing_from_malformed();
        std::cout << "AoSPlay score contract: presets, forms, leaderboard and profile passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AoSPlay score contract failure: " << error.what() << '\n';
        return 1;
    }
}
