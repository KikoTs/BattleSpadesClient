#include "battlespades/network/server_discovery.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void endpoints_are_strict_and_retail_local_is_supported() {
    battlespades::network::ServerEndpoint endpoint;
    std::string error;
    expect(battlespades::network::default_game_port == 27015U &&
               battlespades::network::retail_game_port == 32887U,
           "BattleSpades servers listen on 27015; 32887 is retail's default");
    expect(battlespades::network::parse_server_endpoint("local", endpoint, error) &&
               endpoint.host == "127.0.0.1" && endpoint.port == 27015U,
           "the local alias resolves to the port BattleSpades servers use");
    expect(battlespades::network::parse_server_endpoint("play.example.net", endpoint, error) &&
               endpoint.identifier() == "aos://play.example.net:27015",
           "a bare host means port 27015");
    expect(battlespades::network::parse_server_endpoint(
               " aos://88.80.155.252 ", endpoint, error) &&
               endpoint.host == "88.80.155.252" && endpoint.port == 27015U,
           "a bare address with the scheme means port 27015");
    expect(battlespades::network::parse_server_endpoint(
               "play.example.net:32887", endpoint, error) &&
               endpoint.port == 32887U,
           "an explicit retail port is taken as typed");
    expect(battlespades::network::parse_server_endpoint(
               "play.example.net", endpoint, error, battlespades::network::retail_game_port) &&
               endpoint.port == 32887U,
           "a caller can still ask for the retail default");
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

void gameplay_mode_tag_beats_the_category_mode_tla() {
    // Live AoSPlay list 2026-09-29: mode_tla "dem" on every server because the
    // master read the trailing SERVERMODE_PUBLIC tag (mode=0001).
    constexpr std::string_view json = R"json([
      {"ip":"204.168.157.43","port":27017,"name":"Official CTF","game_mode":"DEM",
       "mode_tla":"dem","tags":["v1.0.0.0","mode=0008","community","mode=0001"]},
      {"ip":"204.168.157.43","port":27018,"name":"Official Zombie","game_mode":"DEM",
       "mode_tla":"dem","tags":["mode=0002","mode=0001"]},
      {"ip":"204.168.157.43","port":27019,"name":"Real Demolition","game_mode":"DEM",
       "mode_tla":"dem","tags":["mode=0001","mode=0001"]},
      {"ip":"204.168.157.43","port":27020,"name":"Plain TDM","mode_tla":"tdm",
       "tags":["mode=0001"]}
    ])json";
    const auto parsed = battlespades::network::parse_public_server_list(json);
    expect(parsed && parsed.servers.size() == 4U, "all four rows must parse");
    std::vector<std::string> codes;
    for (const auto& server : parsed.servers) codes.push_back(server.mode_code);
    std::ranges::sort(codes);
    const std::vector<std::string> expected{"ctf", "dem", "tdm", "zom"};
    expect(codes == expected,
           "the gameplay mode tag must replace a category-derived dem label");
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

void friend_server_selection_matches_authoritative_social_ids() {
    using namespace battlespades::network;
    DiscoveryResult source;
    source.error = "upstream warning";
    source.servers = {
        DiscoveredServer{{"127.0.0.1", 32887U}, 0U, 10U, "Friend Relay", "City",
                         "tdm", "europe", {}, "relay-friend"},
        DiscoveredServer{{"example.net", 32887U}, 0U, 20U, "Public", "London",
                         "ctf", "europe", {}, "relay-other"},
    };
    const std::vector<std::string> ids{"relay-friend"};
    const auto selected = select_discovered_servers(std::move(source), ids);
    expect(selected.servers.size() == 1U &&
               selected.servers.front().master_identifier == "relay-friend",
           "Friends browser must retain only authoritative social server ids");
    expect(selected.error == "upstream warning",
           "Friends filtering must preserve discovery failures for the UI");
}

} // namespace

int main() {
    try {
        endpoints_are_strict_and_retail_local_is_supported();
        public_list_parses_real_schema_and_deduplicates();
        gameplay_mode_tag_beats_the_category_mode_tla();
        lan_response_uses_datagram_source_as_authority();
        opaque_lobby_ids_resolve_to_current_endpoints();
        friend_server_selection_matches_authoritative_social_ids();
        std::cout << "6/6 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
