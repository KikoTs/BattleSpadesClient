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
        offline_account_state_is_loaded_and_logout_is_local();
        std::cout << "revival identity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "revival identity tests failed: " << error.what() << '\n';
        return 1;
    }
}
