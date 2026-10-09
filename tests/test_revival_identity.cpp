#include "battlespades/network/revival_identity.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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

void explicit_offline_profiles_never_need_a_master() {
    const auto path = temporary_state();
    RevivalIdentityConfig config;
    config.state_path = path;
    config.api_base = "http://127.0.0.1:1";
    config.offline = true;
    config.offline_profile = "LAN Player";
    const auto before = std::chrono::steady_clock::now();
    std::string id;
    {
        RevivalIdentityService service{config};
        const auto login = service.guest_login();
        expect(login && login.account->offline && !login.account->ranked_eligible &&
               login.account->nickname == "LAN Player" && !service.has_online_session(),
               "explicit offline profile has a local unranked identity");
        id = login.account->legacy_id;
        const auto rejected = service.login("ValidUser", "test-password");
        expect(!rejected && rejected.error_code == "offline_mode", "account requests disabled offline");
        const auto steam = service.steam_login(480U, "aa", "76561198000000001");
        const auto recovered = service.recover_steam_account("76561198000000001", "fixture-code");
        expect(!steam && steam.error_code == "offline_mode" && !recovered && recovered.error_code == "offline_mode",
               "Steam login and recovery must also respect an explicitly offline profile");
    }
    {
        RevivalIdentityService restored{config};
        const auto login = restored.guest_login();
        expect(login && login.account->legacy_id == id, "offline profile persists across processes");
    }
    expect(std::chrono::steady_clock::now() - before < std::chrono::seconds{3},
           "explicit offline login must not wait for HTTP timeout");
    std::error_code error;
    std::filesystem::remove(path, error);
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

void hosted_results_are_scoped_to_their_master() {
    const auto path = temporary_state();
    const auto write_account = [&](std::string_view issuer) {
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << "{\"version\":1,";
        if (!issuer.empty()) stream << "\"api_base\":\"" << issuer << "\",";
        stream << R"("account":{"public_id":"shared-account","nickname":"Fixture"}})";
        stream.close();
        expect(static_cast<bool>(stream), "Could not write issuer isolation fixture");
    };
    const auto directory_for = [&](std::string_view api, std::string_view issuer) {
        write_account(issuer);
        RevivalIdentityConfig config;
        config.api_base = api;
        config.state_path = path;
        config.allow_environment_override = false;
        RevivalIdentityService service{config};
        expect(service.cached_account().has_value(), "Matching issuer must retain the fixture account");
        return service.hosted_results_directory();
    };
    const auto production = directory_for(RevivalIdentityConfig{}.api_base, {});
    expect(production == path.parent_path() / "hosted-results" / "shared-account",
           "Existing production reports must retain their legacy directory");
    const auto first = directory_for("https://first.example", "https://first.example");
    const auto second = directory_for("https://second.example", "https://second.example");
    expect(first != second && first != production && second != production,
           "Masters with overlapping account IDs must not upload or delete each other's queued reports");
    const auto normalized = directory_for("https://FIRST.example/", "https://first.example");
    expect(normalized == first, "Equivalent master origins must recover the same queued reports");

    write_account({});
    RevivalIdentityConfig custom;
    custom.api_base = "https://first.example";
    custom.state_path = path;
    custom.allow_environment_override = false;
    RevivalIdentityService service{custom};
    expect(!service.cached_account().has_value() && service.hosted_results_directory().empty(),
           "Legacy state without an issuer must not carry its account into a custom master");
    std::error_code error;
    std::filesystem::remove(path, error);
}

void hosted_results_retry_fairly_and_stay_account_scoped(
    std::string_view api, const std::filesystem::path& root) {
    RevivalIdentityConfig config;
    config.api_base = api;
    config.state_path = root / "identity.json";
    RevivalIdentityService service{config};
    const auto login = [&](std::string name) {
        expect(static_cast<bool>(service.login(std::move(name), "local-test-password")),
               "Hosted-results mock login failed");
    };
    const auto attempts = [&] {
        std::ifstream input{root / "attempt-count"};
        std::size_t count{};
        input >> count;
        return count;
    };
    const auto write_report = [](const std::filesystem::path& directory, const std::string& id,
                                  unsigned revision = 1U) {
        std::filesystem::create_directories(directory);
        const auto path = directory / (id + ".json");
        std::ofstream output{path};
        output << "{\"event_id\":\"" << id
               << "\",\"relay_lobby_id\":\"fixture-relay\",\"revision\":" << revision << '}';
        output.close();
        expect(static_cast<bool>(output), "Could not create hosted report fixture");
        return path;
    };
    login("OwnerFixture");
    const auto owner_directory = service.hosted_results_directory();
    for (unsigned index{}; index < 40U; ++index)
        static_cast<void>(write_report(owner_directory, "rejected-" + std::to_string(index)));
    const auto first = service.flush_hosted_results();
    expect(first.uploaded == 0U && !first.error.empty() && attempts() == 16U,
           "First rejected batch must stop after 16 requests and retain its errors");
    const auto later = write_report(owner_directory, "valid-later");
    bool uploaded{};
    for (unsigned pass{}; pass < 5U && !uploaded; ++pass) {
        const auto before = attempts();
        const auto result = service.flush_hosted_results();
        expect(attempts() - before <= 16U, "Retry pass exceeded its request budget");
        uploaded = result.uploaded == 1U;
    }
    expect(uploaded && !std::filesystem::exists(later),
           "Failed old reports starved a later valid report across bounded passes");
    for (unsigned index{}; index < 40U; ++index)
        expect(std::filesystem::is_regular_file(owner_directory / ("rejected-" + std::to_string(index) + ".json")),
               "A rejected report was discarded instead of preserved for retry");
    std::stop_source canceled;
    canceled.request_stop();
    const auto before_cancel = attempts();
    expect(service.flush_hosted_results(canceled.get_token()).uploaded == 0U && attempts() == before_cancel,
           "Canceled retry advanced or sent a request");

    login("OtherFixture");
    const auto other_directory = service.hosted_results_directory();
    expect(other_directory != owner_directory, "Fixture accounts must have independent report directories");
    const auto other = write_report(other_directory, "valid-other");
    expect(service.flush_hosted_results().uploaded == 1U && !std::filesystem::exists(other),
           "Account switch reused the previous account's scan instead of its own reports");

    const auto held = write_report(other_directory, "held-other");
    auto upload = std::async(std::launch::async, [&] { return service.flush_hosted_results(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
    while (!std::filesystem::exists(root / "upload-started") && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    expect(std::filesystem::exists(root / "upload-started"), "Held old-account request never started");
    // A concurrent flush must not duplicate the in-flight batch or block on it.
    const auto before_duplicate = attempts();
    const auto started = std::chrono::steady_clock::now();
    expect(service.flush_hosted_results().uploaded == 0U && attempts() == before_duplicate &&
               std::chrono::steady_clock::now() - started < std::chrono::milliseconds{100},
           "Concurrent uploader duplicated or blocked on the current batch");
    login("OwnerFixture");
    static_cast<void>(write_report(other_directory, "held-other", 2U));
    std::ofstream{root / "release-upload"} << "ready";
    expect(upload.get().uploaded == 0U && std::filesystem::exists(held),
           "Old acknowledgement removed a replaced report after the account switched");
    const auto before_owner = attempts();
    static_cast<void>(service.flush_hosted_results());
    expect(attempts() - before_owner <= 16U && std::filesystem::exists(held),
           "New account processed the old account's durable report");
    login("OtherFixture");
    expect(service.flush_hosted_results().uploaded == 1U && !std::filesystem::exists(held),
           "Returning to the original account did not recover its changed report");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view{argv[1]} == "--steam-identity-mock") {
            RevivalIdentityConfig config;
            config.api_base = argv[2];
            const auto root = std::filesystem::path{argv[3]};
            config.state_path = root / "identity.json";
            config.recovery_directory = root / "Documents" / "BattleSpades";
            config.allow_environment_override = false;
            RevivalIdentityService service{config};
            const std::string steam_id{"76561198000000001"};
            const auto signed_in = service.steam_login(224540U, "aa", steam_id);
            expect(signed_in && signed_in.account->steam_id == steam_id &&
                   signed_in.account->nickname == "PermanentName" && signed_in.account->display_name == "Steam Persona",
                   "verified provider display must not overwrite the registered account name");
            expect(!signed_in.recovery_backup_path.empty() && signed_in.recovery_backup_error.empty(),
                   "new account recovery must be saved outside the installation");
            std::ifstream backup{signed_in.recovery_backup_path};
            const std::string contents{std::istreambuf_iterator<char>{backup}, {}};
            expect(contents.find(steam_id) != std::string::npos && contents.find(signed_in.recovery_code) != std::string::npos,
                   "backup must contain the stable Steam ID and recovery code");
            const auto repeated = service.steam_login(480U, "bb", steam_id);
            expect(repeated && repeated.recovery_code.empty() && repeated.recovery_backup_path.empty(),
                   "ordinary sign-in must not rotate a code or make duplicate backup files");
            const auto mismatch = service.steam_login(224540U, "cc", steam_id);
            expect(!mismatch && mismatch.error_code == "steam_identity_mismatch" && service.cached_account()->steam_id == steam_id,
                   "a mismatched auth response must preserve the previous identity");
            const auto invalid = service.steam_login(123U, "aa", steam_id);
            expect(!invalid && service.has_online_session(), "unsupported app must fail before disturbing the session");
            const auto recovered = service.recover_steam_account(steam_id, signed_in.recovery_code);
            expect(recovered && !recovered.recovery_code.empty() && recovered.recovery_code != signed_in.recovery_code &&
                   recovered.recovery_backup_path != signed_in.recovery_backup_path && std::filesystem::exists(signed_in.recovery_backup_path),
                   "recovery must save its replacement without overwriting any existing file");
            std::filesystem::remove(recovered.recovery_backup_path);
            const auto restored = service.steam_login(224540U, "bb", steam_id);
            expect(restored && restored.recovery_code == recovered.recovery_code && !restored.recovery_backup_path.empty(),
                   "missing backup can be restored from protected state without rotating the server code");
            std::filesystem::remove(restored.recovery_backup_path);
            const auto rotated_elsewhere = service.steam_login(224540U, "dd", steam_id);
            expect(rotated_elsewhere && rotated_elsewhere.recovery_code.empty() && rotated_elsewhere.recovery_backup_path.empty(),
                   "a backup invalidated by recovery on another PC must never be re-exported as current");
            std::ifstream state{config.state_path};
            const std::string protected_state{std::istreambuf_iterator<char>{state}, {}};
            expect(protected_state.find(recovered.recovery_code) == std::string::npos,
                   "recovery code must not be plaintext in launcher state");
            auto failed_state_config = config;
            const auto occupied = root / "not-a-directory";
            { std::ofstream file{occupied}; file << "fixture"; }
            failed_state_config.state_path = occupied / "identity.json";
            RevivalIdentityService failed_state_service{failed_state_config};
            const auto disk_failure = failed_state_service.steam_login(224540U, "aa", steam_id);
            expect(disk_failure && failed_state_service.has_online_session() &&
                   !disk_failure.recovery_code.empty() && !disk_failure.recovery_backup_path.empty() &&
                   !disk_failure.recovery_backup_error.empty(),
                   "state write failure must preserve the accepted session and display/save its recovery code");
            auto linking_config = config;
            linking_config.state_path = root / "linking.json";
            RevivalIdentityService linking{linking_config};
            expect(!linking.steam_login(224540U, "bb", steam_id, true),
                   "linking without an existing account session must fail locally");
            expect(static_cast<bool>(linking.login("ExistingPlayer", "local-test-password")),
                   "existing account must be authenticated before linking");
            const auto wrong_link = linking.steam_login(224540U, "ee", steam_id, true);
            expect(!wrong_link && linking.cached_account()->public_id == "existing-account" &&
                   linking.cached_account()->steam_id.empty(),
                   "link response for another account must never replace the old session");
            const auto linked = linking.steam_login(224540U, "bb", steam_id, true);
            expect(linked && linked.account->public_id == "existing-account" &&
                   linked.account->registered_name == "ExistingPlayer" && linked.account->steam_id == steam_id,
                   "explicit linking sends the existing session and keeps its stable account ID");
#if !defined(_WIN32)
            const auto permissions = std::filesystem::status(config.state_path).permissions();
            expect((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
                       std::filesystem::perms::none,
                   "portable launcher state containing credentials must be owner-only");
#endif
            std::cout << "Steam account identity and private recovery backup checks passed\n";
            return 0;
        }
        if (argc == 4 && std::string_view{argv[1]} == "--hosted-results-mock") {
            hosted_results_retry_fairly_and_stay_account_scoped(argv[2], argv[3]);
            std::cout << "hosted result fairness and account isolation passed\n";
            return 0;
        }
        if (argc == 4 && std::string_view{argv[1]} == "--workshop-mock") {
            RevivalIdentityConfig config;
            config.api_base = argv[2];
            const std::filesystem::path root{argv[3]};
            config.state_path = root / "test-identity.json";
            RevivalIdentityService service{config};
            expect(static_cast<bool>(service.login("FakeBuilder", "local-test-password")), "mock login failed");
            const auto frozen = battlespades::network::read_revival_workshop_project(root, "Map.ugc");
            expect(frozen.files.size() == 3U && frozen.files[1].sha256 ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "publication must checksum the exact frozen VXL bytes");
            bool rejected{};
            try { static_cast<void>(battlespades::network::read_revival_workshop_project(root, "../Map.ugc")); }
            catch (const std::exception&) { rejected = true; }
            expect(rejected, "publication must reject project path traversal");
            const auto lost = service.publish_ugc_project(root, "Map.ugc", "Test Map");
            expect(!lost, "lost final response must not manufacture success");
            const auto recovered = service.publish_ugc_project(root, "Map.ugc", "Test Map");
            expect(recovered && recovered.item_url == config.api_base + "/workshop/test-map-1234",
                "retry must recover the committed item page");
            expect(std::filesystem::is_regular_file(root / "Map.ugc.publication.json"),
                "confirmed publication must persist its local receipt");
            expect(!service.publish_ugc_project(root, "Map.ugc", "Invalid Page"),
                "the archive cannot redirect account-bearing workflow to an arbitrary page");
            expect(!service.publish_ugc_project(root, "Map.ugc", "Invalid Upload"),
                "mismatched object targets must fail before any external upload");
            return 0;
        }
        validation_matches_public_contract();
        explicit_offline_profiles_never_need_a_master();
        offline_account_state_is_loaded_and_logout_is_local();
        hosted_results_are_scoped_to_their_master();
        std::cout << "revival identity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "revival identity tests failed: " << error.what() << '\n';
        return 1;
    }
}
