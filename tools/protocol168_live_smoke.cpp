#include "battlespades/network/enet_protocol168_client.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

namespace {

template <typename Value>
void print_numeric_list(std::string_view name, std::span<const Value> values) {
    std::cout << name << '=';
    for (std::size_t index{}; index < values.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << static_cast<unsigned>(values[index]);
    }
    std::cout << '\n';
}

void print_string_list(std::string_view name,
                       std::span<const std::string> values) {
    std::cout << name << '=';
    for (std::size_t index{}; index < values.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << '"' << values[index] << '"';
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char** argv) {
    battlespades::network::EnetProtocol168Config endpoint;
    battlespades::network::Protocol168SessionConfig session_config;
    if (argc > 1) endpoint.host = argv[1];
    if (argc > 2) {
        unsigned int port{};
        const std::string value{argv[2]};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                            port);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            port == 0U || port > 65'535U) {
            std::cerr << "invalid port\n";
            return 2;
        }
        endpoint.port = static_cast<std::uint16_t>(port);
    }
    if (argc > 3) session_config.player_name = argv[3];
    battlespades::network::Protocol168Session session{session_config};
    const auto result = battlespades::network::run_enet_protocol168_session(
        endpoint, session);
    if (!result.ready) {
        std::cerr << "Protocol168 smoke failed: " << result.error
                  << " received=" << result.received_datagrams
                  << " sent=" << result.sent_datagrams << '\n';
        return 1;
    }
    const auto* info = session.initial_info();
    const auto* map = session.map();
    std::cout << "Protocol168 ready server='"
              << (info != nullptr ? info->server_name : std::string{})
              << "' map='" << (info != nullptr ? info->map_name : std::string{})
              << "' player=" << static_cast<unsigned>(*session.local_player_id())
              << " solid_voxels=" << (map != nullptr ? map->solid_voxels() : 0U)
              << " compressed_map_bytes=" << session.compressed_map_bytes()
              << " roster=" << session.roster().players().size()
              << " received=" << result.received_datagrams
              << " sent=" << result.sent_datagrams << '\n';
    if (const auto* local = session.roster().player(*session.local_player_id());
        local != nullptr) {
        std::cout << "local_class=" << static_cast<unsigned>(local->class_id)
                  << " local_team=" << static_cast<unsigned>(local->team)
                  << " local_name=\"" << local->name << "\"\n";
        print_numeric_list(
            "local_loadout",
            std::span<const std::uint8_t>{local->loadout.data(),
                                          local->loadout.size()});
        print_string_list(
            "local_prefabs",
            std::span<const std::string>{local->prefabs.data(),
                                         local->prefabs.size()});
    }
    if (const auto* state = session.state_info(); state != nullptr) {
        print_string_list(
            "state_prefabs",
            std::span<const std::string>{state->prefabs.data(),
                                         state->prefabs.size()});
    }
    return 0;
}
