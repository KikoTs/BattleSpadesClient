#include "battlespades/network/revival_identity.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::network::RevivalIdentityConfig;
using battlespades::network::RevivalIdentityService;
using battlespades::network::valid_revival_registration_password;
using battlespades::network::valid_revival_username;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::filesystem::path temporary_state() {
    return std::filesystem::temp_directory_path() /
           ("battlespades-identity-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            ".json");
}

void validation_matches_public_contract() {
    expect(valid_revival_username("VoxelBuilder"),
           "valid ASCII username should pass");
    expect(valid_revival_username("A_1"),
           "minimum account username should pass");
    expect(!valid_revival_username("2Builder"),
           "username must start with a letter");
    expect(!valid_revival_username("no spaces"),
           "username alphabet must remain protocol-safe");
    expect(valid_revival_registration_password(
               "correct horse battery staple", "VoxelBuilder"),
           "long unrelated password should pass");
    expect(!valid_revival_registration_password(
               "password1234", "VoxelBuilder"),
           "known common password must fail");
    expect(!valid_revival_registration_password(
               "VoxelBuilder-strong-password", "VoxelBuilder"),
           "password containing username must fail");
}

void offline_account_state_is_loaded_and_logout_is_local() {
    const auto path = temporary_state();
    {
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << R"({
  "version": 1,
  "account": {
    "public_id": "",
    "legacy_id": "LOCAL-TEST1234",
    "nickname": "Guest-TEST1234",
    "account_type": "guest",
    "identity_type": "guest_offline",
    "ranked_eligible": false,
    "offline": true
  }
})";
    }

    RevivalIdentityConfig config;
    config.state_path = path;
    RevivalIdentityService service{config};
    const auto cached = service.cached_account();
    expect(cached.has_value() && cached->nickname == "Guest-TEST1234" &&
               cached->offline,
           "version-one launcher state should restore its offline guest");
    const auto refresh = service.refresh();
    expect(refresh && refresh.account->nickname == "Guest-TEST1234",
           "offline guest refresh must never require HTTP");
    const auto logout = service.logout();
    expect(logout.error.empty() && !service.cached_account().has_value(),
           "logout must clear local account state without a bearer token");

    std::error_code error;
    std::filesystem::remove(path, error);
}

} // namespace

int main() {
    try {
        validation_matches_public_contract();
        offline_account_state_is_loaded_and_logout_is_local();
        std::cout << "revival identity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "revival identity tests failed: " << error.what() << '\n';
        return 1;
    }
}
