#include "battlespades/platform/local_server_process.hpp"

#include <iostream>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

// Reuse the test executable as a tiny real child on every platform. The
// bounded lifetime also collects it if the ownership regression reappears.
int fixture_child(const std::filesystem::path& config) {
    const auto prefix = std::filesystem::current_path() / config.parent_path().filename();
#if defined(_WIN32)
    BOOL in_job{};
    if (IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) && in_job)
        std::ofstream{prefix.string() + ".owned"} << "job";
#endif
    std::ofstream{prefix.string() + ".started"} << "ready";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    std::string input;
    while (std::chrono::steady_clock::now() < deadline) {
        char buffer[32]{};
#if defined(_WIN32)
        const auto handle = GetStdHandle(STD_INPUT_HANDLE);
        DWORD available{};
        if (!PeekNamedPipe(handle, nullptr, 0U, nullptr, &available, nullptr)) break;
        DWORD bytes{};
        if (available != 0U && ReadFile(handle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytes, nullptr))
            input.append(buffer, bytes);
#else
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);
        timeval timeout{};
        if (select(STDIN_FILENO + 1, &readable, nullptr, nullptr, &timeout) > 0) {
            const auto bytes = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (bytes <= 0) break;
            input.append(buffer, static_cast<std::size_t>(bytes));
        }
#endif
        if (input == "shutdown\n") {
            std::ofstream{prefix.string() + ".stopped"} << "shutdown";
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return 0;
}

void move_assignment_stops_replaced_child_and_moved_from_owner_restarts(
    const std::filesystem::path& executable) {
    namespace fs = std::filesystem;
    using battlespades::platform::LocalServerProcess;
    const auto root = fs::temp_directory_path() / ("aos-move-owner-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup final {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{root};
    const auto bundle = root / "bundle";
    fs::create_directories(bundle / "_internal");
    fs::create_directory(bundle / "maps");
#if defined(_WIN32)
    fs::copy_file(executable, bundle / "BattleSpades.exe");
#else
    fs::copy_file(executable, bundle / "BattleSpades");
    fs::permissions(bundle / "BattleSpades", fs::perms::owner_all);
#endif
    battlespades::platform::LocalServerLaunchConfig config;
    config.bundle_root = bundle;
    config.session_parent = root / "sessions";
    const auto marker = [&](const fs::path& session, std::string_view suffix) {
        return bundle / (session.filename().string() + std::string{suffix});
    };
    const auto wait_ready = [&](const LocalServerProcess& process) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!fs::exists(marker(process.session_directory(), ".started")) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        expect(fs::exists(marker(process.session_directory(), ".started")) && process.running(),
               "Owned child fixture did not become ready");
#if defined(_WIN32)
        expect(fs::exists(marker(process.session_directory(), ".owned")),
               "Server code began running before Windows process-tree ownership was established");
#endif
    };
    LocalServerProcess destination;
    LocalServerProcess source;
    std::string error;
    expect(destination.start(config, error), "Destination child did not launch");
    wait_ready(destination);
    const auto old_session = destination.session_directory();
    expect(source.start(config, error), "Source child did not launch");
    wait_ready(source);
    const auto transferred_session = source.session_directory();
    destination = std::move(source);
    expect(fs::exists(marker(old_session, ".stopped")) && !fs::exists(old_session),
           "Move assignment must stop the replaced child and remove its session");
    expect(destination.running() && destination.session_directory() == transferred_session &&
               !source.running() && source.port() == 0U,
           "Move assignment must transfer the surviving child exactly once");
    expect(source.start(config, error), "Moved-from owner could not host a new child");
    wait_ready(source);
    const auto reused_session = source.session_directory();
    source.stop();
    destination.stop();
    expect(fs::exists(marker(reused_session, ".stopped")) &&
               fs::exists(marker(transferred_session, ".stopped")) &&
               !fs::exists(reused_session) && !fs::exists(transferred_session),
           "Reused and transferred owners must each clean their own process/session");
}

void discovers_the_newest_executable_in_complete_release_layouts() {
    namespace fs = std::filesystem;
    using battlespades::platform::find_local_server_bundle;
    const auto parent = fs::temp_directory_path();
    const auto root = parent / ("aos-host-bundle-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    expect(fs::equivalent(root.parent_path(), parent) && fs::create_directory(root),
           "Could not create an isolated bundle-discovery fixture");
    try {
#if defined(_WIN32)
        constexpr auto executable = "BattleSpades.exe";
#else
        constexpr auto executable = "BattleSpades";
#endif
        const auto older = root / "release-dist" / "old";
        const auto newer = root / "local-release-current" / "current";
        const auto incomplete = root / "release-dist-broken" / "incomplete";
        const auto now = fs::file_time_type::clock::now();
        for (const auto& path : {older, newer, incomplete}) {
            fs::create_directories(path / "_internal");
            std::ofstream{path / executable} << "fixture";
        }
        fs::create_directory(older / "maps");
        fs::create_directory(newer / "maps");
        fs::last_write_time(older / executable, now - std::chrono::hours{24});
        fs::last_write_time(newer / executable, now - std::chrono::hours{1});
        fs::last_write_time(incomplete / executable, now);
        fs::last_write_time(older, now + std::chrono::hours{1});
        expect(find_local_server_bundle(root) == newer,
               "Touched old release folders must not override a newer complete executable");
        expect(find_local_server_bundle(older) == older,
               "Explicit bundle paths must retain their requested version");
        fs::create_directory(root / "server");
        fs::create_directory(root / "server" / "_internal");
        fs::create_directory(root / "server" / "maps");
        std::ofstream{root / "server" / executable} << "fixture";
        expect(find_local_server_bundle(root) == root / "server",
               "An executable-adjacent server bundle must win over developer discovery");
        expect(!find_local_server_bundle(root / "missing"),
               "Missing bundle roots must return unavailable without throwing");
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
    fs::remove_all(root);
}

void serializes_one_disposable_match_without_public_services() {
    battlespades::platform::LocalServerLaunchConfig config;
    config.server_name = "KikoTs's Lobby";
    config.mode = "ctf";
    config.map_name = "TokyoNeon";
    config.maximum_players = 16U;
    config.match_minutes = 30U;
    config.bot_count = 4U;
    config.bot_difficulty = "hard";
    config.rule_overrides = {
        {"RULE_CTF_SCORE_TARGET", "5"},
        {"RULE_ENABLE_PREFABS", "OFF"},
    };
    const auto toml = battlespades::platform::build_local_server_toml(config, 27018U);
    expect(toml.find("port = 27018") != std::string::npos &&
               toml.find("default_mode = \"ctf\"") != std::string::npos &&
               toml.find("default_map = \"TokyoNeon\"") != std::string::npos &&
               toml.find("match_length_minutes = 30") != std::string::npos &&
               toml.find("fill_target = 4") != std::string::npos &&
               toml.find("RULE_CTF_SCORE_TARGET = \"5\"") != std::string::npos &&
               toml.find("[steam]\nenabled = false") != std::string::npos &&
               toml.find("[revival]\nenabled = false") != std::string::npos,
           "Create Match TOML must preserve the selected map/mode/rules and remain private");
}

void native_status_rejects_foreign_partial_and_oversized_snapshots() {
    namespace fs = std::filesystem;
    using namespace battlespades::platform;
    const auto root = fs::temp_directory_path() / ("session-status-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto write = [&](const std::string& body) { std::ofstream{root / "host-status.json"} << body; };
    const auto valid = std::string{"{\"schema_version\":1,\"session\":\""} + root.filename().string() +
        "\",\"port\":27015,\"mode\":\"tdm\",\"state\":\"ready\"}";
    try {
        expect(read_local_server_status(root, 27015U, "tdm") == LocalServerState::unavailable, "Absent bridge implies readiness");
        write(valid);
        expect(read_local_server_status(root, 27015U, "tdm") == LocalServerState::ready, "Matching bridge snapshot rejected");
        expect(read_local_server_status(root, 27016U, "tdm") == LocalServerState::unavailable, "Foreign endpoint accepted");
        expect(read_local_server_status(root, 27015U, "ctf") == LocalServerState::unavailable, "Foreign mode accepted");
        auto stale = valid; stale.replace(stale.find(root.filename().string()), root.filename().string().size(), "session-stale");
        write(stale);
        expect(read_local_server_status(root, 27015U, "tdm") == LocalServerState::unavailable, "Foreign session accepted");
        write(valid.substr(0U, valid.size() / 2U));
        expect(read_local_server_status(root, 27015U, "tdm") == LocalServerState::unavailable, "Partial snapshot accepted");
        write(std::string(4097U, 'x'));
        expect(read_local_server_status(root, 27015U, "tdm") == LocalServerState::unavailable, "Oversized bridge accepted");
    } catch (...) { fs::remove_all(root); throw; }
    fs::remove_all(root);
}

void rejects_untrusted_mode_and_rule_names() {
    battlespades::platform::LocalServerLaunchConfig config;
    config.mode = "../server";
    expect(battlespades::platform::build_local_server_toml(config, 27015U).empty(),
           "unknown mode identifiers must fail closed");
    config.mode = "tdm";
    config.rule_overrides.emplace("not_a_rule", "ON");
    expect(battlespades::platform::build_local_server_toml(config, 27015U).empty(),
           "non-retail rule keys must fail closed");
}

void serializes_the_isolated_map_creator_program() {
    using namespace battlespades::platform;
    LocalServerLaunchConfig config;
    config.program = LocalServerProgram::map_creator;
    config.server_name = "Map Creator - Castle";
    config.mode = "ugc";
    config.map_name = "Castle";
    config.bot_count = 0U;
    config.map_creator = LocalMapCreatorLaunchConfig{
        "Castle", "water", "ctf", "Castle", "KikoTs",
        std::filesystem::path{"C:/AoS/hosted_ugc"},
        std::filesystem::path{"C:/AoS/assets/original"},
        std::uint8_t{0U},
    };
    const auto toml = build_local_server_toml(config, 27022U);
    expect(toml.find("default_mode = \"ugc\"") != std::string::npos &&
               toml.find("[map_creator]") != std::string::npos &&
               toml.find("project = \"Castle\"") != std::string::npos &&
               toml.find("terrain = \"water\"") != std::string::npos &&
               toml.find("target_mode = \"ctf\"") != std::string::npos &&
               toml.find("prefab_set = 0") != std::string::npos &&
               toml.find("publish_root = \"C:/AoS/hosted_ugc\"") != std::string::npos,
           "Map Creator TOML must preserve project/baseplate/mode paths separately");

    config.map_creator->terrain = "../../escape";
    expect(build_local_server_toml(config, 27022U).empty(),
           "Map Creator must reject unknown baseplates before launching a process");
}

void enables_public_identity_only_for_a_complete_relay_contract() {
    battlespades::platform::LocalServerLaunchConfig config;
    config.environment_overrides = {
        {"AOS_MASTER_URL", "https://aosplay.net"},
        {"AOS_MASTER_WRITE_TOKEN", "aos_lobby_secret-not-for-toml"},
        {"AOS_PUBLIC_HOST", "relay.aosplay.net"},
        {"AOS_PUBLIC_PORT", "31000"},
        {"AOS_PUBLIC_QUERY_PORT", "31000"},
        {"AOS_SERVER_ID", "relay.aosplay.net:31000"},
    };
    const auto toml =
        battlespades::platform::build_local_server_toml(config, 27015U);
    expect(toml.find("[revival]\nenabled = true\nrequire_identity = true") !=
               std::string::npos,
           "a complete relay contract must enable AoSPlay ticket validation");
    expect(toml.find("aos_lobby_secret-not-for-toml") == std::string::npos,
           "ephemeral relay credentials must never be serialized to disk");

    config.environment_overrides.erase("AOS_PUBLIC_QUERY_PORT");
    expect(battlespades::platform::build_local_server_toml(config, 27015U).empty(),
           "partial relay environments must fail closed");
    config.environment_overrides["AOS_PUBLIC_QUERY_PORT"] = "31000";
    config.environment_overrides["PATH"] = "C:/untrusted";
    expect(battlespades::platform::build_local_server_toml(config, 27015U).empty(),
           "arbitrary child environment injection must be rejected");
}

#if !defined(_WIN32)
struct PosixServerFixture final {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("aos process fixture " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path bundle = root / "server bundle";

    explicit PosixServerFixture(std::string_view script) {
        std::filesystem::create_directories(bundle / "_internal");
        std::filesystem::create_directory(bundle / "maps");
        const auto executable = bundle / "BattleSpades";
        std::ofstream{executable} << "#!/bin/sh\nset -eu\n" << script;
        std::filesystem::permissions(executable, std::filesystem::perms::owner_all);
    }

    ~PosixServerFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    battlespades::platform::LocalServerLaunchConfig config() const {
        battlespades::platform::LocalServerLaunchConfig result;
        result.bundle_root = std::filesystem::relative(bundle);
        result.session_parent = root / "sessions";
        return result;
    }
};

void wait_for_fixture_file(const std::filesystem::path& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!std::filesystem::is_regular_file(path) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    expect(std::filesystem::is_regular_file(path), "The launched POSIX fixture never became ready");
}

void launches_relative_posix_bundle_with_private_environment_and_graceful_control() {
    namespace fs = std::filesystem;
    PosixServerFixture fixture{R"sh(
printf '%s\n' "$PWD" "$1" "$2" "$3" "$AOS_NATIVE_HOST_STATUS" "$AOS_NATIVE_HOST_SESSION" "$AOS_PUBLIC_HOST" > launch.tmp
mv launch.tmp launch.txt
IFS= read -r command
printf '%s\n' "$command" > stopped.txt
)sh"};
    auto config = fixture.config();
    config.environment_overrides = {
        {"AOS_MASTER_URL", "https://example.invalid"},
        {"AOS_MASTER_WRITE_TOKEN", "fixture-only"},
        {"AOS_PUBLIC_HOST", "client-launch.test"},
        {"AOS_PUBLIC_PORT", "31000"},
        {"AOS_PUBLIC_QUERY_PORT", "31000"},
        {"AOS_SERVER_ID", "client-launch.test:31000"},
    };
    const auto parent_environment = [] {
        const auto* value = std::getenv("AOS_PUBLIC_HOST");
        return value == nullptr ? std::optional<std::string>{} : std::optional<std::string>{value};
    };
    const auto previous_environment = parent_environment();
    const auto previous_directory = fs::current_path();
    battlespades::platform::LocalServerProcess server;
    std::string error;
    expect(server.start(config, error), "A relative POSIX server bundle must launch");
    wait_for_fixture_file(fixture.bundle / "launch.txt");
    std::ifstream stream{fixture.bundle / "launch.txt"};
    std::string directory, config_flag, config_path, control_flag, status_path, session, public_host;
    std::getline(stream, directory);
    std::getline(stream, config_flag);
    std::getline(stream, config_path);
    std::getline(stream, control_flag);
    std::getline(stream, status_path);
    std::getline(stream, session);
    std::getline(stream, public_host);
    expect(fs::equivalent(directory, fixture.bundle) && config_flag == "--config" &&
               fs::equivalent(config_path, server.session_directory() / "config.toml") &&
               control_flag == "--control-stdin",
           "Child cwd and spaced absolute arguments must identify the original bundle/session");
    expect(fs::path{status_path} == server.session_directory() / "host-status.json" &&
               session == server.session_directory().filename().string() && public_host == "client-launch.test",
           "The child must receive its native bridge and allowlisted environment");
    expect(parent_environment() == previous_environment && fs::current_path() == previous_directory,
           "Launching a server must leave the multithreaded client's environment and cwd unchanged");
    const auto session_path = server.session_directory();
    server.stop();
    std::ifstream stopped{fixture.bundle / "stopped.txt"};
    std::string command;
    std::getline(stopped, command);
    expect(command == "shutdown" && !server.running() && !fs::exists(session_path),
           "Control stdin must gracefully stop the owned child and remove its disposable session");
}

void closed_posix_control_does_not_raise_sigpipe_in_the_client() {
    PosixServerFixture fixture{R"sh(
exec 0<&-
printf ready > ready.txt
sleep 0.2
)sh"};
    battlespades::platform::LocalServerProcess server;
    std::string error;
    expect(server.start(fixture.config(), error), "The closed-stdin fixture must launch");
    wait_for_fixture_file(fixture.bundle / "ready.txt");
    const auto previous_signal = std::signal(SIGPIPE, SIG_DFL);
    server.stop();
    std::signal(SIGPIPE, previous_signal);
    expect(!server.running(), "A closed control reader must not terminate the client during cleanup");
}

void restarting_an_exited_posix_server_releases_the_previous_session() {
    PosixServerFixture fixture{"printf ready > ready.txt\nexit 0\n"};
    battlespades::platform::LocalServerProcess server;
    std::string error;
    expect(server.start(fixture.config(), error), "The early-exit fixture must launch");
    wait_for_fixture_file(fixture.bundle / "ready.txt");
    const auto first_session = server.session_directory();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (server.running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    expect(!server.running(), "The first fixture must exit before restarting");
    std::ofstream{fixture.bundle / "BattleSpades"} << "#!/bin/sh\nprintf ready > restarted.txt\nread command\n";
    expect(server.start(fixture.config(), error), "An exited server must be restartable on the same owner");
    wait_for_fixture_file(fixture.bundle / "restarted.txt");
    expect(!std::filesystem::exists(first_session) && server.session_directory() != first_session,
           "Restart must clean the dead child's session before publishing the replacement");
    server.stop();
}

#if defined(__APPLE__)
void macos_reports_spawn_failure_before_claiming_to_host() {
    PosixServerFixture fixture{"exit 0\n"};
    std::filesystem::permissions(fixture.bundle / "BattleSpades", std::filesystem::perms::owner_read);
    battlespades::platform::LocalServerProcess server;
    std::string error;
    expect(!server.start(fixture.config(), error) && !error.empty() && !server.running(),
           "macOS must report executable permission errors synchronously");
    expect(std::filesystem::is_empty(fixture.root / "sessions"),
           "A failed macOS spawn must not leave a disposable session behind");
}

void macos_cleans_owned_helpers_after_the_server_leader_exits() {
    PosixServerFixture fixture{R"sh(
sh -c 'printf "%s\n" "$$" > helper.tmp; mv helper.tmp helper.pid; exec sleep 30' &
while [ ! -f helper.pid ]; do sleep 0.01; done
exit 0
)sh"};
    battlespades::platform::LocalServerProcess server;
    std::string error;
    expect(server.start(fixture.config(), error), "The helper fixture must launch");
    wait_for_fixture_file(fixture.bundle / "helper.pid");
    int helper{};
    std::ifstream{fixture.bundle / "helper.pid"} >> helper;
    expect(helper > 0 && kill(helper, 0) == 0, "The server's helper must be running before cleanup");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (server.running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    expect(!server.running(), "The server leader must exit while its helper remains alive");
    server.stop();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (kill(helper, 0) == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    expect(kill(helper, 0) < 0, "Stopping an exited macOS server must collect its owned helper group");
}
#endif
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view{argv[1]} == "--config" &&
        std::string_view{argv[3]} == "--control-stdin") return fixture_child(argv[2]);
    try {
        move_assignment_stops_replaced_child_and_moved_from_owner_restarts(std::filesystem::absolute(argv[0]));
        native_status_rejects_foreign_partial_and_oversized_snapshots();
        discovers_the_newest_executable_in_complete_release_layouts();
        serializes_one_disposable_match_without_public_services();
        rejects_untrusted_mode_and_rule_names();
        serializes_the_isolated_map_creator_program();
        enables_public_identity_only_for_a_complete_relay_contract();
#if !defined(_WIN32)
        launches_relative_posix_bundle_with_private_environment_and_graceful_control();
        closed_posix_control_does_not_raise_sigpipe_in_the_client();
        restarting_an_exited_posix_server_releases_the_previous_session();
#if defined(__APPLE__)
        macos_reports_spawn_failure_before_claiming_to_host();
        macos_cleans_owned_helpers_after_the_server_leader_exits();
#endif
#endif
        std::cout << "Local server process: disposable config and validation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Local server process failure: " << error.what() << '\n';
        return 1;
    }
}
