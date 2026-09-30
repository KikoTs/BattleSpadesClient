// Headless check of the server password exchange through the real live
// connection worker: PasswordNeeded(112) -> PasswordProvided(113) ->
// InitialInfo. Usage:
//   aos_protocol168_password_live_smoke HOST PORT PASSWORD [MODE]
// MODE: prompt (default)  wait for the prompt, answer wrong once, then right
//       supplied          give the password up front, expect no prompt
//       wrong             answer wrong until the server kicks
#include "battlespades/network/live_protocol168_connection.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace battlespades::network;
using Clock = std::chrono::steady_clock;

[[nodiscard]] bool ended(const LiveProtocol168Status& status) {
    return status.phase == LiveProtocol168Phase::failed ||
           status.phase == LiveProtocol168Phase::disconnected;
}

/** Wait until a prompt is pending; false when the connection ended first. */
[[nodiscard]] bool wait_for_prompt(LiveProtocol168Connection& connection,
                                   std::uint8_t requests, std::chrono::seconds limit) {
    const auto deadline = Clock::now() + limit;
    while (Clock::now() < deadline) {
        const auto status = connection.status();
        if (status.password.pending && status.password.requests >= requests) return true;
        if (ended(status) || status.initial_info != nullptr) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "usage: aos_protocol168_password_live_smoke HOST PORT PASSWORD "
                     "[prompt|supplied|wrong]\n";
        return 2;
    }
    const auto port = std::strtoul(argv[2], nullptr, 10);
    if (port == 0UL || port > 65'535UL) return 2;
    const std::string password{argv[3]};
    const std::string_view mode{argc == 5 ? argv[4] : "prompt"};

    LiveProtocol168Connection connection;
    Protocol168SessionConfig session;
    session.player_name = "NativePassword";
    session.auto_join = false;
    if (mode == "supplied") session.server_password = password;
    if (!connection.start(
            EnetProtocol168Config{argv[1], static_cast<std::uint16_t>(port), 30'000U},
            session)) {
        std::cerr << connection.status().error << '\n';
        return 1;
    }

    if (mode == "wrong") {
        std::uint8_t request{1U};
        while (wait_for_prompt(connection, request, std::chrono::seconds{10})) {
            std::this_thread::sleep_for(std::chrono::milliseconds{600});
            if (!connection.provide_password("definitely-wrong")) {
                std::cerr << "the answer was not accepted for sending\n";
                return 1;
            }
            ++request;
        }
        const auto deadline = Clock::now() + std::chrono::seconds{10};
        while (Clock::now() < deadline && !ended(connection.status())) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        const auto status = connection.status();
        const auto failure = protocol168_password_failure(status);
        std::cout << "wrong: requests=" << static_cast<unsigned>(status.password.requests)
                  << " reason="
                  << (status.disconnect_reason ? static_cast<long>(*status.disconnect_reason)
                                               : -1L)
                  << " verdict="
                  << (failure == PasswordJoinFailure::wrong_password ? "wrong_password"
                      : failure == PasswordJoinFailure::timed_out  ? "timed_out"
                                                                   : "none")
                  << '\n';
        return failure == PasswordJoinFailure::wrong_password ? 0 : 1;
    }

    if (mode == "prompt") {
        if (!wait_for_prompt(connection, 1U, std::chrono::seconds{10})) {
            std::cerr << "no password prompt: " << connection.status().error << '\n';
            return 1;
        }
        auto status = connection.status();
        if (status.password.rejected || status.initial_info != nullptr) {
            std::cerr << "the first prompt must be plain and come before InitialInfo\n";
            return 1;
        }
        std::cout << "prompt 1 shown\n";
        // The no-progress timer must wait with the prompt: sit on it a while.
        std::this_thread::sleep_for(std::chrono::seconds{2});
        if (!connection.provide_password("definitely-wrong")) return 1;
        if (!wait_for_prompt(connection, 2U, std::chrono::seconds{10}) ||
            !connection.status().password.rejected) {
            std::cerr << "a wrong answer must reopen the prompt as rejected\n";
            return 1;
        }
        std::cout << "prompt 2 shown (rejected)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds{600});
        if (!connection.provide_password(password)) return 1;
    }

    const auto deadline = Clock::now() + std::chrono::seconds{40};
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    while (Clock::now() < deadline && bootstrap == nullptr) {
        const auto status = connection.status();
        if (ended(status)) {
            std::cerr << "ended: " << status.error << '\n';
            return 1;
        }
        if (status.password.pending) {
            std::cerr << "an unexpected prompt (request "
                      << static_cast<unsigned>(status.password.requests) << ")\n";
            return 1;
        }
        bootstrap = connection.take_bootstrap();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (bootstrap == nullptr || bootstrap->map == nullptr) {
        std::cerr << "no world bootstrap after the password\n";
        return 1;
    }
    const auto status = connection.status();
    std::cout << mode << ": joined server='" << bootstrap->initial_info.server_name
              << "' map='" << bootstrap->initial_info.map_name
              << "' requests=" << static_cast<unsigned>(status.password.requests)
              << " solid_voxels=" << bootstrap->map->solid_voxels() << '\n';
    connection.stop();
    return 0;
}
