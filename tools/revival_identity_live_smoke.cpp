#include "battlespades/network/revival_identity.hpp"

#include <exception>
#include <iostream>

int main() {
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Identity smoke failed: " << error.what() << '\n';
        return 1;
    }
}
