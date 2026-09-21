#include "battlespades/network/revival_identity.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    try {
        battlespades::network::RevivalIdentityService identity;
        const auto cached = identity.cached_account();
        if (!cached.has_value() || !identity.has_online_session()) {
            std::cerr << "No protected online AoSPlay session is available.\n";
            return 2;
        }
        const auto refreshed = identity.refresh();
        if (!refreshed) {
            std::cerr << "Identity refresh failed: " << refreshed.error << '\n';
            return 1;
        }
        const auto& account = *refreshed.account;
        std::cout << "Authenticated " << account.nickname
                  << " (" << account.account_type << ", "
                  << (account.ranked_eligible ? "ranked" : "unranked")
                  << ")\n";
        if (argc == 3 && std::string_view{argv[1]} == "--social-search") {
            battlespades::network::RevivalSocialRequest request;
            request.generation = 1U;
            request.kind = battlespades::network::RevivalSocialRequestKind::find_friends;
            request.query = argv[2];
            const auto social = identity.social_request(request);
            if (!social) {
                std::cerr << "Social search failed: " << social.error << '\n';
                return 1;
            }
            std::cout << "Social search returned " << social.found_players.size()
                      << " profile(s)";
            for (const auto& profile : social.found_players) {
                std::cout << "\n- "
                          << (profile.nickname.empty() ? profile.username : profile.nickname)
                          << " [" << profile.friendship_status << "]";
            }
            std::cout << '\n';
        } else if (argc == 4 && std::string_view{argv[1]} == "--presence-server") {
            battlespades::network::RevivalSocialRequest request;
            request.generation = 1U;
            request.kind = battlespades::network::RevivalSocialRequestKind::sync;
            request.cursor = "0";
            request.client_instance_id = argv[2];
            request.presence = "in_game";
            request.payload = nlohmann::json{{"server_id", argv[3]}};
            const auto social = identity.social_request(request);
            if (!social) {
                std::cerr << "Presence publish failed: " << social.error << '\n';
                return 1;
            }
            std::cout << "Published protected in-game presence for " << argv[3] << '\n';
        } else if (argc == 3 && std::string_view{argv[1]} == "--presence-offline") {
            battlespades::network::RevivalSocialRequest request;
            request.generation = 1U;
            request.kind = battlespades::network::RevivalSocialRequestKind::presence_offline;
            request.client_instance_id = argv[2];
            const auto social = identity.social_request(request);
            if (!social) {
                std::cerr << "Presence cleanup failed: " << social.error << '\n';
                return 1;
            }
            std::cout << "Cleared protected presence lease\n";
        } else if (argc == 2 && std::string_view{argv[1]} == "--relay-smoke") {
            battlespades::network::RevivalRelayLobbyRequest request;
            request.name = "BattleSpades Relay Contract Smoke";
            request.map = "AncientEgypt";
            request.game_mode = "TDM_TITLE";
            request.mode_tla = "tdm";
            request.max_players = 4U;
            request.texture_skin = "classic";
            const auto allocated = identity.create_relay_lobby(request);
            if (!allocated || !allocated.lobby.has_value()) {
                std::cerr << "Relay allocation failed: " << allocated.error << '\n';
                return 1;
            }
            const auto lobby = *allocated.lobby;
            std::cout << "Allocated relay " << lobby.server_id << '\n';
            if (!identity.close_relay_lobby(lobby)) {
                std::cerr << "Relay cleanup failed\n";
                return 1;
            }
            std::cout << "Released relay allocation\n";
        } else if (argc != 1) {
            std::cerr << "Usage: aos_revival_identity_live_smoke "
                         "[--social-search QUERY | --presence-server UUID SERVER_ID | "
                         "--presence-offline UUID | --relay-smoke]\n";
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Identity smoke failed: " << error.what() << '\n';
        return 1;
    }
}
