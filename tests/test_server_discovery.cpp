#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "battlespades/network/server_discovery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

class LoopbackUdp final {
public:
    LoopbackUdp() {
#if defined(_WIN32)
        WSADATA data{};
        expect(WSAStartup(MAKEWORD(2, 2), &data) == 0, "start Winsock fixture");
#endif
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        expect(socket_ != invalid, "create UDP fixture");
        address_.sin_family = AF_INET;
        address_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        expect(bind(socket_, reinterpret_cast<const sockaddr*>(&address_), sizeof(address_)) == 0,
               "bind UDP fixture");
        Length length{sizeof(address_)};
        expect(getsockname(socket_, reinterpret_cast<sockaddr*>(&address_), &length) == 0,
               "read fixture port");
    }
    ~LoopbackUdp() {
#if defined(_WIN32)
        closesocket(socket_);
        WSACleanup();
#else
        close(socket_);
#endif
    }
    LoopbackUdp(const LoopbackUdp&) = delete;
    LoopbackUdp& operator=(const LoopbackUdp&) = delete;
    std::uint16_t port() const { return ntohs(address_.sin_port); }
    std::string receive(sockaddr_in& source, long timeout_ms = 1'000) const {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket_, &readable);
        timeval wait{};
        wait.tv_sec = static_cast<decltype(wait.tv_sec)>(timeout_ms / 1'000);
        wait.tv_usec = static_cast<decltype(wait.tv_usec)>((timeout_ms % 1'000) * 1'000);
        if (select(static_cast<int>(socket_ + 1), &readable, nullptr, nullptr, &wait) <= 0) return {};
        std::array<char, 128> bytes{};
        Length length{sizeof(source)};
        const auto size = recvfrom(socket_, bytes.data(), static_cast<BufferLength>(bytes.size()), 0,
                                   reinterpret_cast<sockaddr*>(&source), &length);
        return size > 0 ? std::string{bytes.data(), static_cast<std::size_t>(size)} : std::string{};
    }
    void send(std::string_view bytes, const sockaddr_in& to) const {
        const auto sent = sendto(socket_, bytes.data(), static_cast<BufferLength>(bytes.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        expect(sent >= 0 && static_cast<std::size_t>(sent) == bytes.size(),
               "send fixture response");
    }
private:
#if defined(_WIN32)
    using Socket = SOCKET;
    using Length = int;
    using BufferLength = int;
    static constexpr Socket invalid{INVALID_SOCKET};
#else
    using Socket = int;
    using Length = socklen_t;
    using BufferLength = std::size_t;
    static constexpr Socket invalid{-1};
#endif
    Socket socket_{invalid};
    sockaddr_in address_{};
};

void classic_ping_measures_exact_hello_hi_on_game_ports() {
    using namespace battlespades::network;
    using namespace std::chrono_literals;
    LoopbackUdp v75, v76, unrelated;
    std::vector<DiscoveredServer> rows{
        {{"127.0.0.1", v75.port(), GameProtocol::classic075}},
        {{"127.0.0.1", v76.port(), GameProtocol::classic076}},
        {{"127.0.0.1", v75.port(), GameProtocol::classic076}},
        {{"127.0.0.1", unrelated.port(), GameProtocol::retail168}},
        {{"does-not-resolve.invalid", v75.port(), GameProtocol::classic075}},
    };
    rows[0].query_port = unrelated.port();
    rows[3].ping_milliseconds = 41U;
    auto replies = std::async(std::launch::async, [&] {
        sockaddr_in first{}, second{}, ignored{};
        // Both requests must arrive before either response: no serial timeouts.
        expect(v75.receive(first) == "HELLO", "0.75 request is exactly five ASCII bytes");
        expect(v76.receive(second) == "HELLO", "0.76 uses the same raw ping protocol");
        unrelated.send("HI", first);
        v75.send(std::string_view{"HI\0", 3}, first);
        v75.send("HI\n", first);
        v75.send("{}", first);
        std::this_thread::sleep_for(70ms);
        v75.send("HI", first);
        v75.send("HI", first);
        v76.send("HI", second);
        expect(v75.receive(ignored, 20).empty(), "duplicate rows share one probe");
        expect(unrelated.receive(ignored, 20).empty(), "retail and query ports must not get Classic pings");
    });
    measure_classic_server_pings(rows, 900ms);
    replies.get();
    expect(rows[0].ping_known && rows[1].ping_known && rows[2].ping_known,
           "both Classic protocols and duplicate listings receive measurements");
    expect(rows[0].ping_milliseconds >= 60U && rows[1].ping_milliseconds >= 60U &&
               rows[0].ping_milliseconds == rows[2].ping_milliseconds,
           "report full RTT; reject malformed, duplicate and wrong-port replies");
    expect(rows[3].ping_known && rows[3].ping_milliseconds == 41U && !rows[4].ping_known,
           "preserve retail measurements and leave unsupported DNS rows unknown");
}

void classic_ping_timeout_and_cancellation_are_bounded() {
    using namespace battlespades::network;
    using namespace std::chrono_literals;
    const std::array<LoopbackUdp, 6> silent;
    std::vector<DiscoveredServer> rows;
    for (const auto& endpoint : silent) {
        rows.push_back(DiscoveredServer{{"127.0.0.1", endpoint.port(), GameProtocol::classic075}});
    }
    const auto started = std::chrono::steady_clock::now();
    measure_classic_server_pings(rows, 120ms);
    expect(std::chrono::steady_clock::now() - started < 600ms,
           "unreachable targets share one deadline, not six consecutive waits");
    expect(std::ranges::none_of(rows, [](const auto& row) { return row.ping_known; }),
           "timeouts leave rows unknown and available to join");
    std::stop_source cancellation;
    std::jthread cancel{[&] { std::this_thread::sleep_for(40ms); cancellation.request_stop(); }};
    const auto cancelling = std::chrono::steady_clock::now();
    measure_classic_server_pings(rows, 4s, cancellation.get_token());
    expect(std::chrono::steady_clock::now() - cancelling < 600ms,
           "a cancelled refresh must not wait for the network timeout");
    sockaddr_in source{};
    for (const auto& endpoint : silent) {
        while (!endpoint.receive(source, 0).empty()) {}
    }
    measure_classic_server_pings(rows, 4s, cancellation.get_token());
    expect(silent[0].receive(source, 20).empty(), "pre-cancelled refresh sends no probes");
}

void classic_master_ping_is_not_a_local_measurement() {
    using namespace battlespades::network;
    const auto parsed = parse_public_server_list(R"([
        {"ip":"127.0.0.1","port":32887,"game_version":"0.75","ping":12},
        {"ip":"127.0.0.1","port":32888,"game_version":"0.76","ping":null},
        {"ip":"127.0.0.1","port":27015,"ping":null}
    ])");
    expect(parsed && parsed.servers.size() == 3U &&
               std::ranges::none_of(parsed.servers, [](const auto& row) { return row.ping_known; }),
           "Classic master RTT and missing pings must never masquerade as local measurements");
}

void endpoints_are_strict_and_retail_local_is_supported() {
    battlespades::network::ServerEndpoint endpoint;
    std::string error;
    expect(battlespades::network::parse_server_endpoint("aosbb://127.0.0.1:27015", endpoint, error) &&
               endpoint.host == "127.0.0.1" && endpoint.port == 27015U &&
               endpoint.protocol == battlespades::network::GameProtocol::retail168,
           "game-specific URI selects retail protocol");
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
       "tags":["mode=0001"]},
      {"ip":"204.168.157.43","port":27021,"name":"Official Classic CTF","game_mode":"DEM",
       "mode_tla":"dem","tags":["mode=0008","gamemode=CCTF","mode=0001"]},
      {"ip":"204.168.157.43","port":27022,"name":"Unknown Gamemode","game_mode":"DEM",
       "mode_tla":"dem","tags":["gamemode=foo","mode=0009","mode=0001"]}
    ])json";
    const auto parsed = battlespades::network::parse_public_server_list(json);
    expect(parsed && parsed.servers.size() == 6U, "all six rows must parse");
    std::vector<std::string> codes;
    for (const auto& server : parsed.servers) codes.push_back(server.mode_code);
    std::ranges::sort(codes);
    const std::vector<std::string> expected{"cctf", "ctf", "dem", "tc", "tdm", "zom"};
    expect(codes == expected,
           "a known gamemode tag must beat the ordinal, and an unknown one must fall back to it");
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

void steam_rows_parse_like_master_rows_and_merge() {
    using battlespades::network::SteamListedServer;
    std::vector<SteamListedServer> steam{
        // Real rows as Steam returns them (2026-10-05): retail port, mode in
        // the map prefix, playlist=8 and the category mode=0001 on everyone.
        {"15.235.106.95", 32887U, 32887U, "Revival Official | NA CCTF", "CCTF_Crossroads",
         "v168;playlist=8;region=america;mode=0001;classic", 10U, 24U, 4U, false, 90000000000001ULL, 40},
        {"5.6.7.8", 32887U, 0U, "Relay host", "ZOM_Atlantis",
         "v168;playlist=8;mode=0001;sdr=85568392936826697;sdr480=90294212260083732", 2U, 16U, 0U, true,
         90000000000002ULL, 0},
    };
    const auto parsed = battlespades::network::parse_steam_server_list(steam);
    expect(parsed && parsed.servers.size() == 2U, "both Steam rows must parse");
    const auto& official = parsed.servers[0];
    expect(official.mode_code == "cctf" && official.map == "Crossroads" && official.classic &&
               official.region == "us_east" && official.human_players == 6U && official.steam_listed &&
               official.steam_host_id == 0U,
           "Steam rows decode mode from the map prefix, region aliases and humans");
    const auto& relay = parsed.servers[1];
    expect(relay.mode_code == "zom" && relay.steam_host_id == 85568392936826697ULL &&
               relay.steam_host_id_spacewar == 90294212260083732ULL && relay.dedicated_relay_host &&
               relay.steam_server_id == 90000000000002ULL && relay.password_protected,
           "sdr= and sdr480= tags carry the relay host ids for each Steam application");
    expect(!official.dedicated_relay_host && official.steam_host_id_spacewar == 0U,
           "a server without relay hosts advertises none");

    // AoSPlay lists the same server at its game port.
    battlespades::network::DiscoveryResult aosplay;
    aosplay.servers.push_back(official);
    aosplay.servers.back().steam_listed = false;
    aosplay.servers.back().game.port = 27015U;
    aosplay.servers.back().name = "AoSPlay name";
    const auto merged = battlespades::network::merge_discovered_servers(aosplay, parsed);
    expect(merged.servers.size() == 2U && merged.servers[0].name == "AoSPlay name" &&
               merged.servers[0].steam_listed,
           "an address both lists know keeps the AoSPlay row; Steam adds the rest");
    // A registered SteamID matches even across hosts that differ (NAT/IP).
    battlespades::network::DiscoveryResult registered;
    registered.servers.push_back(official);
    registered.servers.back().game.host = "10.0.0.1";
    registered.servers.back().name = "Renamed";
    registered.servers.back().steam_listed = false;
    registered.servers.back().steam_server_id = 90000000000001ULL;
    const auto by_id = battlespades::network::merge_discovered_servers(registered, parsed);
    expect(by_id.servers.size() == 2U && by_id.servers[0].steam_listed,
           "the registered SteamID ties a Steam row to its AoSPlay row");
    // The AoSPlay list carries the same ids as fields.
    const auto listed = battlespades::network::parse_public_server_list(
        R"json([{"ip":"9.9.9.9","port":27015,"name":"Listed","mode_tla":"tdm","tags":[],
                 "steam_host_id":"85568392936826697","steam_host_id_480":"90294212260083732"}])json");
    expect(listed && listed.servers.size() == 1U &&
               listed.servers[0].steam_host_id == 85568392936826697ULL &&
               listed.servers[0].steam_host_id_spacewar == 90294212260083732ULL &&
               listed.servers[0].dedicated_relay_host,
           "relay host ids arrive from the AoSPlay list as fields");

    battlespades::network::DiscoveryResult failed;
    failed.error = "public server list request failed";
    expect(battlespades::network::merge_discovered_servers(failed, parsed).error.empty(),
           "Steam rows make the browser usable when AoSPlay failed");
}

void saved_list_answers_when_the_master_is_unreachable() {
    const auto cache = std::filesystem::temp_directory_path() / "aos_serverlist_cache_test.json";
    {
        std::ofstream out{cache, std::ios::binary | std::ios::trunc};
        out << R"json([{"ip":"1.2.3.4","port":27015,"name":"Saved","mode_tla":"tdm","tags":[]}])json";
    }
    battlespades::network::PublicDiscoveryConfig config;
    config.url = "https://127.0.0.1:9/serverlist/";
    config.timeout = std::chrono::milliseconds{800};
    config.cache_file = cache;
    const auto result = battlespades::network::discover_public_servers(config);
    expect(result && result.from_cache && result.servers.size() == 1U &&
               result.servers[0].name == "Saved",
           "an unreachable master falls back to the saved list");
    std::filesystem::remove(cache);
    const auto none = battlespades::network::discover_public_servers(config);
    expect(!none && none.servers.empty(), "no saved list: the failure is reported");
}

int main() {
    try {
        endpoints_are_strict_and_retail_local_is_supported();
        public_list_parses_real_schema_and_deduplicates();
        gameplay_mode_tag_beats_the_category_mode_tla();
        lan_response_uses_datagram_source_as_authority();
        opaque_lobby_ids_resolve_to_current_endpoints();
        friend_server_selection_matches_authoritative_social_ids();
        steam_rows_parse_like_master_rows_and_merge();
        saved_list_answers_when_the_master_is_unreachable();
        classic_ping_measures_exact_hello_hi_on_game_ports();
        classic_ping_timeout_and_cancellation_are_bounded();
        classic_master_ping_is_not_a_local_measurement();
        std::cout << "11/11 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
