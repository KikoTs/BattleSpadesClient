#include "battlespades/platform/discord_presence.hpp"

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace battlespades::platform;
bool await_ack(DiscordPresence& presence, DiscordActivityAcknowledgement expected, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (presence.status() == DiscordPresenceStatus::ready && presence.activity_acknowledgement() == expected) return true;
        std::this_thread::sleep_for(25ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view{argv[1]} != "--publish-and-clear") {
        std::cerr << "Usage: aos_discord_presence_probe --publish-and-clear\n"
                     "Briefly publishes a verification activity to the local Discord desktop client, then clears it.\n"
                     "No server address, join action, messages or credentials are sent.\n";
        return 2;
    }
    DiscordPresence presence;
    presence.configure({.allow_join = false});
    presence.update({.mode = "Checking Discord integration", .players = 0});
    const bool published = await_ack(presence, DiscordActivityAcknowledgement::published, 10s);
    presence.clear();
    const bool cleared = published && await_ack(presence, DiscordActivityAcknowledgement::cleared, 5s);
    std::cout << "application_id=" << configured_discord_application_id()
              << " published_ack=" << (published ? "true" : "false")
              << " cleared_ack=" << (cleared ? "true" : "false") << '\n';
    return published && cleared ? 0 : 1;
}
