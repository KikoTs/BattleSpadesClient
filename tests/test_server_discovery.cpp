#include "battlespades/network/server_discovery.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void endpoints_are_strict_and_retail_local_is_supported() {
    battlespades::network::ServerEndpoint endpoint;
    std::string error;
    expect(battlespades::network::parse_server_endpoint("local", endpoint, error) &&
               endpoint.host == "127.0.0.1" && endpoint.port == 32887U,
           "retail local alias must resolve to the historical port");
    expect(battlespades::network::parse_server_endpoint(
               "aos://play.example.net:27015", endpoint, error) &&
               endpoint.identifier() == "aos://play.example.net:27015",
           "scheme, DNS host and explicit port must round-trip");
    expect(!battlespades::network::parse_server_endpoint("host:0", endpoint, error) &&
               !error.empty(),
           "zero ports must fail closed");
    expect(!battlespades::network::parse_server_endpoint("host:12:34", endpoint, error),
           "ambiguous IPv6-shaped input must not reach the IPv4 ENet transport");
}

void public_list_parses_real_schema_and_deduplicates() {
    constexpr std::string_view json = R"json([
      {"identifier":"88.80.155.252:38886","name":"Zombie EU",
       "ip":"88.80.155.252","port":38886,"queryPort":38887,
       "players":12,"max_players":24,"map":"LunarBase",
       "game_mode":"ZOM","mode_tla":"zom","region":"europe",
       "official":true,"ping":41,"tags":["classic","identity=ticket-v1"]},
      {"identifier":"88.80.155.252:38886","name":"duplicate",
       "ip":"88.80.155.252","port":38886},
      {"name":"malformed"}
    ])json";
    const auto parsed = battlespades::network::parse_public_server_list(json);
    expect(parsed && parsed.servers.size() == 1U,
           "public list must discard malformed and duplicate endpoints");
    const auto& server = parsed.servers.front();
    expect(server.name == "Zombie EU" && server.game.port == 38886U &&
               server.query_port == 38887U && server.mode_code == "zom" &&
               server.classic && server.official && server.players == 12U &&
               server.master_identifier == "88.80.155.252:38886" &&
               server.identity_ticket,
           "AoSPlay metadata must retain browser and loading fields");
}

void lan_response_uses_datagram_source_as_authority() {
    constexpr std::string_view json =
        R"json({"name":"LAN Match","players_current":3,"players_max":16,"map":"Classic","game_mode":"CCTF"})json";
    const auto parsed = battlespades::network::parse_lan_server_response(
        json, battlespades::network::ServerEndpoint{"192.168.1.10", 27015U}, 7U);
    expect(parsed && parsed.servers.size() == 1U, "valid HELLOLAN JSON must produce one row");
    const auto& server = parsed.servers.front();
    expect(server.local && server.classic && server.game.host == "192.168.1.10" &&
               server.game.port == 27015U && server.ping_milliseconds == 7U,
           "untrusted LAN JSON must not override the observed UDP endpoint");
}

void opaque_lobby_ids_resolve_to_current_endpoints() {
    using battlespades::network::DiscoveredServer;
    const std::array servers{
        DiscoveredServer{{"relay-a.aosplay.net", 30100U}, 30100U, 31U, "Lobby A",
                         "London", "tdm", "europe", {}, "relay-id-a"},
        DiscoveredServer{{"relay-b.aosplay.net", 30200U}, 30200U, 42U, "Lobby B",
                         "Chicago", "ctf", "europe", {}, "relay-id-b"}};
    const auto opaque = battlespades::network::find_discovered_server(servers, "relay-id-b");
    expect(opaque.has_value() && opaque->game.port == 30200U,
           "social lobby IDs must resolve through authoritative master metadata");
    const auto endpoint = battlespades::network::find_discovered_server(
        servers, "aos://relay-a.aosplay.net:30100");
    expect(endpoint.has_value() && endpoint->master_identifier == "relay-id-a",
           "explicit endpoint identifiers remain valid for compatibility");
    expect(!battlespades::network::find_discovered_server(servers, "expired").has_value(),
           "expired lobby IDs must fail closed instead of becoming DNS names");
}

} // namespace

int main() {
    try {
        endpoints_are_strict_and_retail_local_is_supported();
        public_list_parses_real_schema_and_deduplicates();
        lan_response_uses_datagram_source_as_authority();
        opaque_lobby_ids_resolve_to_current_endpoints();
        std::cout << "4/4 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
