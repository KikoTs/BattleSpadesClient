#include "battlespades/network/aosplay_scores.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string player = argc > 1 ? argv[1] : "KikoTs";
    battlespades::network::AosPlayScoreServiceConfig config;
    const auto leaderboard = battlespades::network::fetch_aosplay_leaderboard(
        config, 0U, battlespades::network::AosPlayLeaderboardScope::global);
    if (!leaderboard) {
        std::cerr << "leaderboard live smoke failed: " << leaderboard.error << '\n';
        return 1;
    }
    std::string error;
    const auto account =
        battlespades::network::resolve_aosplay_account_id(config, player, error);
    if (!account.has_value()) {
        std::cerr << "identity live smoke failed: " << error << '\n';
        return 1;
    }
    const auto profile =
        battlespades::network::fetch_aosplay_profile(config, *account);
    if (!profile || !profile.profile.has_value()) {
        std::cerr << "profile live smoke failed: "
                  << (profile.error.empty() ? "profile not found" : profile.error) << '\n';
        return 1;
    }
    std::cout << "AoSPlay live: rows=" << leaderboard.rows.size()
              << " account=" << *account
              << " player=" << profile.profile->player_name << '\n';
    return 0;
}
