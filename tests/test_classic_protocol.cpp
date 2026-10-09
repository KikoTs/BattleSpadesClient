#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/frontend/loading_screen.hpp"
#include "battlespades/network/classic_protocol.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/network/server_discovery.hpp"
#include "battlespades/world/classic_combat.hpp"
#include "battlespades/world/classic_corpse.hpp"
#include "battlespades/world/classic_movement.hpp"
#include "battlespades/world/classic_weapons.hpp"
#include "battlespades/world/player_inventory.hpp"
#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <zlib.h>

using namespace battlespades;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> bool runtime_packet_is(const std::vector<std::byte>& bytes) {
    const auto decoded = network::decode_runtime_packet(bytes);
    return decoded && std::holds_alternative<T>(*decoded.packet);
}
struct Packet {
    std::vector<std::byte> bytes;
    explicit Packet(std::uint8_t id) {
        byte(id);
    }
    void byte(std::uint8_t b) {
        bytes.push_back(static_cast<std::byte>(b));
    }
    template <class T> void integer(T n) {
        for (std::size_t i = 0; i < sizeof(T); ++i)
            byte(static_cast<std::uint8_t>(static_cast<std::uint64_t>(n) >> (i * 8U)));
    }
    void number(float n) {
        integer(std::bit_cast<std::uint32_t>(n));
    }
    void vector(float x, float y, float z) {
        number(x);
        number(y);
        number(z);
    }
    void text(std::string_view s) {
        for (auto b : s)
            byte(static_cast<std::uint8_t>(b));
        byte(0);
    }
};
std::vector<std::byte> flat_map() {
    std::vector<std::byte> raw;
    raw.reserve(512 * 512 * 20);
    for (int n = 0; n < 512 * 512; ++n) {
        for (auto b : {0, 60, 63, 0})
            raw.push_back(static_cast<std::byte>(b));
        for (int z = 60; z < 64; ++z)
            for (auto b : {0, 255, 0, 128})
                raw.push_back(static_cast<std::byte>(b));
    }
    return raw;
}
std::unique_ptr<network::Protocol168WorldBootstrap>
bootstrap(network::ClassicProtocolSession& session,
          const std::vector<std::byte>& raw,
          bool territory_mode = false,
          network::ClassicPackets* events = nullptr,
          bool hidden_objectives = false,
          const network::ClassicPackets& before_state = {}) {
    uLongf count = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::byte> zipped(count);
    check(compress2(reinterpret_cast<Bytef*>(zipped.data()),
                    &count,
                    reinterpret_cast<const Bytef*>(raw.data()),
                    static_cast<uLong>(raw.size()),
                    1) == Z_OK,
          "fixture compression");
    zipped.resize(count);
    Packet start{18};
    start.integer(static_cast<std::uint32_t>(zipped.size()));
    if (session.protocol() == network::GameProtocol::classic076) {
        start.integer<std::uint32_t>(0x12345678U);
        start.text("Fixture 0.76");
    }
    auto first = session.ingest(start.bytes);
    check(first.error.empty() && first.map_started, "map starts");
    if (session.protocol() == network::GameProtocol::classic076)
        check(first.wire.size() == 1 &&
                  first.wire[0] == std::vector<std::byte>({std::byte{31}, std::byte{0}}),
              "076 cache miss handshake");
    Packet chunk{19};
    chunk.bytes.insert(chunk.bytes.end(), zipped.begin(), zipped.end());
    check(session.ingest(chunk.bytes).error.empty(), "map chunk");
    for (const auto& packet : before_state)
        check(session.ingest(packet).error.empty(), "deferred bootstrap player");
    Packet state{15};
    state.byte(0);
    for (int n = 0; n < 9; ++n)
        state.byte(128);
    for (int n = 0; n < 20; ++n)
        state.byte(0);
    state.byte(territory_mode ? 1 : 0);
    if (territory_mode) {
        state.byte(3);
        for (int n = 0; n < 3; ++n) {
            state.vector(40.5F + n * 100, 60.5F, 59);
            state.byte(static_cast<std::uint8_t>(n));
        }
    } else {
        state.byte(0);
        state.byte(0);
        state.byte(10);
        state.byte(0);
        for (int n = 0; n < 4; ++n) {
            if (hidden_objectives)
                state.vector(std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity(),
                             128);
            else
                state.vector(40.5F + n * 100, 60.5F, 59);
        }
    }
    auto result = session.ingest(state.bytes);
    check(result.error.empty() && result.bootstrap != nullptr, "CTF bootstrap");
    if (session.protocol() == network::GameProtocol::classic076)
        check(result.bootstrap->initial_info.map_name == "Fixture 0.76", "076 map metadata decoded");
    for (const auto& event : result.events)
        check(static_cast<bool>(network::decode_runtime_packet(event)) ||
                  static_cast<bool>(network::decode_create_player(event)) ||
                  static_cast<bool>(network::decode_terrain_packet(event)), "bootstrap bridge decodes");
    if (events)
        *events = std::move(result.events);
    return std::move(result.bootstrap);
}
Packet spawn(std::uint8_t id) {
    Packet p{12};
    p.byte(id);
    p.byte(0);
    p.byte(id % 2);
    p.vector(256.5F, 256.5F, 57.75F);
    p.text("Test");
    return p;
}

void territory_state_padding_tests(const std::vector<std::byte>& raw) {
    // Actual StateData from 152.117.81.19:30002: eight active territories,
    // followed by eight unused slots in piqueserver's fixed 241-byte layout.
    const auto path = std::filesystem::path{CLASSIC_GOLDEN}.parent_path() /
                      "classic-tc-state-voxide.bin";
    std::ifstream stream{path, std::ios::binary};
    const std::vector<char> captured{std::istreambuf_iterator<char>{stream}, {}};
    check(captured.size() == 241, "captured TC StateData fixture");
    std::vector<std::byte> state;
    for (auto value : captured) state.push_back(static_cast<std::byte>(value));

    uLongf size = compressBound(static_cast<uLong>(raw.size()));
    Packet chunk{19};
    chunk.bytes.resize(size + 1);
    check(compress2(reinterpret_cast<Bytef*>(chunk.bytes.data() + 1), &size,
                    reinterpret_cast<const Bytef*>(raw.data()),
                    static_cast<uLong>(raw.size()), 1) == Z_OK, "TC fixture map compression");
    chunk.bytes.resize(size + 1);
    Packet start{18};
    start.integer(static_cast<std::uint32_t>(size));
    const auto ingest = [&](network::GameProtocol version, const std::vector<std::byte>& packet) {
        network::ClassicProtocolSession session{version};
        check(session.ingest(start.bytes).error.empty(), "TC map start");
        check(session.ingest(chunk.bytes).error.empty(), "TC map chunk");
        return session.ingest(packet);
    };
    for (const auto version : {network::GameProtocol::classic075, network::GameProtocol::classic076}) {
        for (const bool padded : {false, true}) {
            auto packet = state;
            if (!padded) packet.resize(33 + 8 * 13);
            const auto result = ingest(version, packet);
            check(result.error.empty() && result.bootstrap &&
                      result.bootstrap->local_player_id == 22 &&
                      result.bootstrap->state_info.mode_type == 9 &&
                      result.bootstrap->state_info.score_limit == 8,
                  "padded and compact TC packets preserve the eight active territories");
            std::size_t zones{};
            for (const auto& event : result.events)
                if (runtime_packet_is<network::TerritoryBaseStatePacket>(event)) ++zones;
            check(zones == 16, "unused territory slots do not create phantom objectives");
        }
        auto malformed = state;
        malformed.pop_back();
        check(!ingest(version, malformed).error.empty(), "reject partial TC padding");
        malformed = state;
        malformed.push_back(std::byte{});
        check(!ingest(version, malformed).error.empty(), "reject unknown TC trailing data");
        malformed = state;
        malformed.resize(33 + 8 * 13 - 1);
        check(!ingest(version, malformed).error.empty(), "reject a truncated active territory");
        malformed = state;
        malformed[32] = std::byte{17};
        check(!ingest(version, malformed).error.empty(), "reject excessive territory count");
    }
}

void protocol_tests(const std::vector<std::byte>& raw) {
    using network::GameProtocol;
    for (auto version : {GameProtocol::classic075, GameProtocol::classic076}) {
        network::ClassicProtocolSession session{version};
        auto boot = bootstrap(session, raw);
        check(boot->map->solid(0, 0, 236), "classic map translated exactly");
        check(boot->map->color(0, 0, 236)->green == 255, "pure green map surface retained");
        check(boot->initial_info.classic && boot->state_info.mode_type == 8, "classic CTF scene");
        Packet existing{9};
        existing.byte(0); // local slot
        existing.byte(0); // team
        existing.byte(0); // rifle
        existing.byte(2); // gun
        existing.integer<std::int32_t>(0);
        for (unsigned i{}; i < 3; ++i) existing.byte(112);
        existing.text("Existing local");
        const auto metadata = session.ingest(existing.bytes);
        const auto metadata_player = network::decode_create_player(metadata.events.front());
        check(metadata.error.empty() && metadata_player && metadata_player.packet->dead,
              "local roster metadata cannot manufacture a spawn");
        network::ClassicMotion premature_motion;
        premature_motion.alive = true;
        check(session.motion(premature_motion, 1).empty(), "no movement before actual local spawn");
        check(session.ingest(spawn(0).bytes).error.empty(), "local spawn");
        check(session.ingest(spawn(1).bytes).error.empty(), "remote spawn");
        check(boot->state_info.team1_show_max_score && boot->state_info.score_limit == 10,
              "native score bar shows the original capture limit");
        const auto color =
            session.translate_client(network::encode_packet(network::SetColorPacket{0, 0xE05020}));
        check(color.size() == 1 && color.front() == std::vector<std::byte>{std::byte{8},
                                                                           std::byte{0},
                                                                           std::byte{0x20},
                                                                           std::byte{0x50},
                                                                           std::byte{0xE0}},
              "palette change uses original BGR SetColor");
        const auto build = session.ingest(network::classic_block_packet(0, 0, {40, 40, 235}));
        const auto built = network::decode_terrain_packet(build.events.front());
        check(built && std::get<network::BlockBuildColoredPacket>(*built.packet).color == 0xE05020,
              "own build keeps chosen color without a server SetColor echo");
        network::Protocol168TerrainReplica terrain{*boot->map, 1, true, false, true};
        const auto line =
            session.ingest(network::classic_line_packet(0, {41, 40, 235}, {43, 40, 235}));
        check(line.error.empty() && line.events.size() == 2,
              "line carries builder palette into native terrain");
        for (const auto& bytes : line.events)
            check(terrain.apply(bytes).error.empty(), "apply original line");
        const auto line_color = boot->map->color(42, 40, 235);
        check(line_color && line_color->red == 0xE0 && line_color->green == 0x50 &&
                  line_color->blue == 0x20,
              "own block line matches chosen palette without an echo");
        Packet world{2};
        if (version == GameProtocol::classic076)
            world.byte(1);
        else {
            world.vector(0, 0, 0);
            world.vector(0, 0, 0);
        }
        world.vector(10, 20, 30);
        world.vector(1, 0, 0);
        auto update = session.ingest(world.bytes);
        check(update.error.empty() && !update.events.empty(), "versioned world update");
        auto decoded = network::decode_world_update_weapon_rows(update.events.front());
        check(decoded && decoded.rows.size() == 1 && decoded.rows[0].player_id == 1 &&
                  decoded.rows[0].position[2] == 206,
              "076 explicit/075 implicit id and position");
        auto malformed = world.bytes;
        malformed.pop_back();
        check(!session.ingest(malformed).error.empty(), "reject short world packet");
        network::ClassicMotion motion;
        motion.alive = true;
        motion.position = {256.5F, 256.5F, 233.75F};
        auto initial = session.motion(motion, 1);
        check(initial.size() == 5, "initial motion fields");
        check(session.motion(motion, 1.5).empty(), "position capped to 1 Hz");
        motion.orientation = {0, 1, 0};
        check(!session.motion(motion, 1.6).empty(), "orientation changed");
        motion.orientation = {0, -1, 0};
        check(session.motion(motion, 1.601).empty(), "orientation rate bounded");
        check(!session.motion(motion, 1.62).empty(), "rate limited orientation retained for send");
        motion.movement = 16;
        auto jump = session.motion(motion, 1.64);
        check(std::any_of(jump.begin(),
                          jump.end(),
                          [](const auto& p) {
                              return p.size() == 3 && p[0] == std::byte{3} &&
                                     (p[2] & std::byte{16}) != std::byte{0};
                          }),
              "accepted jump sent");
        motion.movement = 0;
        check(session.motion(motion, 1.65).empty(),
              "jump is not cancelled within the same server tick");
        check(!session.motion(motion, 1.69).empty(), "jump pulse clears before landing");
        motion.alive = false;
        check(session.motion(motion, 1.7).size() == 2, "release input on death");
        check(session.translate_client({std::array{std::byte{4}, std::byte{0}}}).empty(),
              "retail ClientData never forwarded");
        Packet chat{17};
        chat.byte(1);
        chat.byte(0);
        chat.byte(0x82);
        chat.byte(0);
        auto text = session.ingest(chat.bytes);
        auto parsed = network::decode_runtime_packet(text.events.front());
        check(parsed && std::get<network::ChatMessagePacket>(*parsed.packet).value == "\xc3\xa9",
              "CP437 converted to UTF8");
        auto next = bootstrap(session, raw);
        check(next->map_generation == 2 &&
                  next->roster.present_players().begin() == next->roster.present_players().end(),
              "map rotation resets roster and generation");
        check(session.motion(premature_motion, 2).empty(), "rotation forgets the previous life");
        network::ClassicPackets redirect_events;
        const auto redirected = bootstrap(session, raw, false, &redirect_events, false,
                                           {existing.bytes, spawn(0).bytes});
        const auto* redirected_local = redirected->roster.player(0);
        check(redirected->map_generation == 3 && redirected_local && !redirected_local->dead &&
                  redirected_local->position.z == 233.75,
              "a redirect retains the authoritative spawn received before StateData");
        check(!session.motion(premature_motion, 3).empty(),
              "server-initiated spawn resumes without sending another join");
    }
    check(network::next_protocol(GameProtocol::retail168, 3, false) == GameProtocol::classic075,
          "168 fallback");
    check(network::next_protocol(GameProtocol::classic075, 3, false) == GameProtocol::classic076,
          "075 fallback");
    for (auto reason : {1U, 4U, 10U, 11U})
        check(!network::next_protocol(GameProtocol::retail168, reason, false),
              "no fallback on ban/full/kick/timeout");
    check(!network::next_protocol(GameProtocol::retail168, 3, true), "no fallback after app data");
    // Voxide accepts connect-data 168 but replies with a raw classic MapStart.
    // Retry explicitly as 0.75 instead of feeding its map into the retail decoder.
    const std::array legacy_start{
        std::byte{18}, std::byte{0}, std::byte{0}, std::byte{24}, std::byte{0}};
    const auto detect = [&](std::span<const std::byte> packet,
                            bool received = false,
                            GameProtocol requested = GameProtocol::automatic,
                            GameProtocol attempted = GameProtocol::retail168) {
        return network::next_protocol_from_bootstrap(requested, attempted, received, packet);
    };
    check(detect(legacy_start) == GameProtocol::classic075,
          "raw classic bootstrap starts explicit negotiation");
    check(!detect(legacy_start, true), "never switch an existing application stream");
    check(!detect(legacy_start, false, GameProtocol::retail168), "pinned retail never switches");
    check(!detect(legacy_start, false, GameProtocol::classic076), "pinned 076 never switches");
    check(!detect(legacy_start, false, GameProtocol::automatic, GameProtocol::classic075),
          "no classic retry loop");
    for (std::size_t size{}; size < legacy_start.size(); ++size)
        check(!detect(std::span{legacy_start}.first(size)),
              "truncated map header is not protocol evidence");
    auto invalid_start = legacy_start;
    invalid_start[0] = std::byte{0x30};
    check(!detect(invalid_start), "retail envelope is not classic");
    invalid_start = legacy_start;
    invalid_start[4] = std::byte{255};
    check(!detect(invalid_start), "unbounded map header is not protocol evidence");
    invalid_start.fill(std::byte{0});
    invalid_start[0] = std::byte{18};
    check(!detect(invalid_start), "empty map header is not protocol evidence");
    auto extended_start = std::vector<std::byte>{legacy_start.begin(), legacy_start.end()};
    extended_start.push_back(std::byte{0});
    check(!detect(extended_start), "unknown map metadata is not guessed");
    network::ServerEndpoint endpoint;
    std::string error;
    check(network::parse_server_endpoint("aos://16777343:32887:0.76", endpoint, error) &&
              endpoint.host == "127.0.0.1" && endpoint.protocol == GameProtocol::classic076,
          "packed IPv4 version link");
    check(endpoint.identifier() == "aos://127.0.0.1:32887:0.76",
          "version preserved through link persistence");
    check(frontend::resolve_protocol168_mode(8, true).title_key == "CLASSIC_CTF_TITLE",
          "Classic+ keeps existing identity");
    check(frontend::resolve_protocol168_mode(8, true, GameProtocol::classic075).title_key ==
              "CLASSIC_075_TITLE",
          "legacy mode label from protocol");
    check(frontend::resolve_protocol168_mode(9, true, GameProtocol::classic076).code == "tc",
          "legacy TC not changed to CTF");
    frontend::MatchLoadingModel loader;
    loader.begin("Test", "CLASSIC_075_TITLE", true, "classic");
    check(loader.snapshot().mode_title_key == "CLASSIC_075_TITLE",
          "prefilled classic protocol title survives loader resolution");
    loader.initial_info("Test", "CLASSIC_076_TITLE", true, "classic");
    check(loader.snapshot().mode_title_key == "CLASSIC_076_TITLE",
          "detected protocol replaces advertised loader title");
    loader.set_classic_scoring(false);
    auto classic_scores = loader.snapshot().score_rows;
    check(classic_scores.size() == 4 && classic_scores[1].value == "+10" &&
              classic_scores[3].value == "+1",
          "original loading screen does not advertise Classic+ scoring bonuses");
    loader.set_classic_scoring(true);
    check(loader.snapshot().score_rows.size() == 2,
          "original territory mode does not advertise intel scoring");
    loader.initial_info("Test", "CLASSIC_CTF_TITLE", true, "classic");
    check(loader.snapshot().score_rows.size() > 4,
          "rejoining Classic+ restores its normal score table");
    const auto rows = network::parse_public_server_list(
        R"([{"identifier":"aos://16777343:32887","game_version":"0.75","name":"Local"},{"identifier":"aos://16777343:32888","game_version":"0.75","name":"Local"},{"identifier":"aos://16777343:32887","game_version":"0.76","name":"Local"}])");
    check(rows.servers.size() == 3, "legacy server list preserves ports and versions");
    network::DiscoveryResult native_rows;
    network::DiscoveredServer native_server;
    native_server.game.host = "127.0.0.1";
    native_server.game.port = 27015;
    native_server.name = "Local";
    native_rows.servers.push_back(native_server);
    check(network::merge_discovered_servers(native_rows, rows).servers.size() == 4,
          "classic and retail servers on one host stay separate");
    auto action = network::classic_block_packet(0, 0, {1, 2, 237});
    check(network::valid_classic_client_action(action), "valid classic action");
    action.pop_back();
    check(!network::valid_classic_client_action(action), "reject truncated outgoing action");
    check(
        !network::valid_classic_client_action(std::array{std::byte{4}, std::byte{0}, std::byte{1}}),
        "raw retail or movement packets cannot bypass adapter");
}

void objective_tests(const std::vector<std::byte>& raw) {
    using namespace network;
    for (auto version : {GameProtocol::classic075, GameProtocol::classic076}) {
        ClassicProtocolSession hidden{version};
        ClassicPackets hidden_events;
        const auto hidden_boot = bootstrap(hidden, raw, false, &hidden_events, true);
        check(hidden_boot && hidden_events.empty(),
              "scripted hidden objectives allow bootstrap without invalid render coordinates");
        Packet show{11};
        show.byte(2);
        show.byte(0);
        show.vector(80, 90, 50);
        const auto shown = hidden.ingest(show.bytes);
        check(shown.error.empty() && shown.events.size() == 1 &&
                  runtime_packet_is<MinimapZonePacket>(shown.events.front()),
              "hidden base can become visible through the existing zone component");
        Packet hide{11};
        hide.byte(2);
        hide.byte(0);
        hide.vector(
            std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 128);
        const auto removed = hidden.ingest(hide.bytes);
        check(removed.error.empty() && removed.events.size() == 1 &&
                  runtime_packet_is<MinimapZoneClearPacket>(removed.events.front()),
              "hiding a visible base clears its existing marker");
        show.bytes[1] = std::byte{0};
        hide.bytes[1] = std::byte{0};
        const auto flag = hidden.ingest(show.bytes);
        check(flag.error.empty() && flag.events.size() == 1 &&
                  runtime_packet_is<CreateEntityPacket>(flag.events.front()),
              "hidden intel can reappear");
        const auto hidden_flag = hidden.ingest(hide.bytes);
        check(hidden_flag.error.empty() && hidden_flag.events.size() == 1 &&
                  runtime_packet_is<DestroyEntityPacket>(hidden_flag.events.front()),
              "hiding intel removes its existing entity");
        Packet invalid{11};
        invalid.byte(0);
        invalid.byte(0);
        invalid.vector(std::numeric_limits<float>::quiet_NaN(), 0, 0);
        check(!hidden.ingest(invalid.bytes).error.empty(),
              "unknown non-finite objective coordinates remain rejected");
        Packet invalid_player{0};
        invalid_player.vector(
            std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 128);
        check(!hidden.ingest(invalid_player.bytes).error.empty(),
              "hidden objective sentinel is never accepted as player physics");
    }
    ClassicProtocolSession ctf{GameProtocol::classic075};
    ClassicPackets events;
    static_cast<void>(bootstrap(ctf, raw, false, &events));
    unsigned intel{}, bases{};
    for (const auto& bytes : events) {
        const auto decoded = decode_runtime_packet(bytes);
        check(decoded && !std::holds_alternative<MinimapBillboardPacket>(*decoded.packet),
              "objectives never add duplicate or custom billboards");
        if (const auto* entity = std::get_if<CreateEntityPacket>(&*decoded.packet)) {
            check(entity->type == 16,
                  "CTF uses existing intel entity, never a fabricated base entity");
            ++intel;
        }
        if (const auto* zone = std::get_if<MinimapZonePacket>(&*decoded.packet)) {
            check(zone->key == 1 && zone->icon_id == 6 && zone->color[0] == 128,
                  "CTF uses existing shared base zone and server colors");
            ++bases;
        }
    }
    check(intel == 2 && bases == 2, "one native marker per objective");
    const auto heartbeat = ctf.ingest(std::array{std::byte{2}});
    check(heartbeat.error.empty() && heartbeat.events.size() == 1 &&
              static_cast<bool>(decode_world_update_weapon_rows(heartbeat.events.front())),
          "empty original WorldUpdate reaches the existing watchdog in a solo match");
    static_cast<void>(ctf.ingest(spawn(0).bytes));
    Packet pickup{24};
    pickup.byte(0);
    auto picked = ctf.ingest(pickup.bytes);
    check(picked.error.empty() && picked.events.size() == 2,
          "pickup removes ground intel and attaches carried intel");
    Packet drop{25};
    drop.byte(0);
    drop.vector(22, 33, 59);
    auto dropped = ctf.ingest(drop.bytes);
    check(dropped.error.empty() && dropped.events.size() == 2, "drop restores one intel entity");
    check(static_cast<bool>(decode_tool_action_packet(dropped.events.back())),
          "drop uses the existing pickup lifecycle");

    ClassicProtocolSession tc{GameProtocol::classic076};
    const auto boot = bootstrap(tc, raw, true, &events);
    check(boot->state_info.mode_type == 9 && boot->state_info.team1_score == 1 &&
              boot->state_info.team2_score == 1,
          "TC retains ownership scores");
    frontend::GameHudModel hud;
    const auto hud_team = [](std::uint8_t team) {
        using Team = frontend::hud_layout::Team;
        return team == 2   ? Team::team1
               : team == 3 ? Team::team2
               : team == 1 ? Team::neutral
                           : Team::spectator;
    };
    const auto apply = [&](const ClassicPackets& packets) {
        for (const auto& bytes : packets) {
            auto decoded = decode_runtime_packet(bytes);
            check(static_cast<bool>(decoded), "TC bridge event decodes");
            if (const auto* state = std::get_if<TerritoryBaseStatePacket>(&*decoded.packet))
                hud.update_territory_base(
                    {state->base_index,
                     static_cast<frontend::GameHudTerritoryBaseAction>(state->action),
                     hud_team(state->controlled_by),
                     hud_team(state->attacked_by),
                     state->capture_amount});
        }
    };
    apply(events);
    check(hud.territory_bases().bases[0]->visible && hud.territory_bases().bases[2]->visible,
          "TC uses existing active HUD bases");
    static_cast<void>(tc.advance(1));
    Packet progress{22};
    progress.byte(0);
    progress.byte(1);
    progress.byte(2);
    progress.number(0.25F);
    auto update = tc.ingest(progress.bytes);
    check(update.error.empty(), "TC capture update");
    apply(update.events);
    apply(tc.advance(2));
    check(std::abs(hud.territory_bases().bases[0]->capture_amount - 0.35) < 0.016,
          "server rate advances existing TC capture display");
    Packet captured{21};
    captured.byte(0);
    captured.byte(0);
    captured.byte(1);
    auto complete = tc.ingest(captured.bytes);
    check(complete.error.empty(), "TC capture completion");
    apply(complete.events);
    check(hud.territory_bases().bases[0]->controlled_by == frontend::hud_layout::Team::team2,
          "TC capture changes native HUD ownership");
    Packet moved{11};
    moved.byte(0);
    moved.byte(0);
    moved.vector(48, 64, 59);
    const auto moved_result = tc.ingest(moved.bytes);
    check(moved_result.error.empty(), "scripted territory move");
    apply(moved_result.events);
    check(hud.territory_bases().bases[0]->controlled_by == frontend::hud_layout::Team::team1,
          "MoveObject ownership changes update both the zone and native TC HUD");
}

void stabilization_tests(const std::vector<std::byte>& raw) {
    using namespace network;
    for (const auto version : {GameProtocol::classic075, GameProtocol::classic076}) {
        ClassicProtocolSession extensions{version};
        Packet extension_map{18}; extension_map.integer<std::uint32_t>(1024);
        check(extensions.ingest(extension_map.bytes).error.empty(), "extension bootstrap starts");
        for (int i = 0; i < 5000; ++i) {
            const auto ignored = extensions.ingest(std::array{std::byte{96}, std::byte{1}, std::byte{20}});
            check(ignored.error.empty() && ignored.events.empty() && ignored.wire.empty(),
                  "unknown extension bursts are ignored instead of filling the bootstrap queue");
        }
        const auto advertised = extensions.ingest(std::array{std::byte{60}, std::byte{3},
            std::byte{32}, std::byte{1}, std::byte{193}, std::byte{1}, std::byte{48}, std::byte{1}});
        check(advertised.error.empty() && advertised.wire == ClassicPackets{{std::byte{60}, std::byte{0}}},
              "server extensions receive an honest empty support list");
        ClassicProtocolSession session{version};
        Packet challenge{31};
        challenge.integer<std::uint32_t>(0x87654321U);
        const auto response = session.ingest(challenge.bytes);
        check(response.error.empty() && response.wire.size() == 1 &&
                  response.wire[0] == std::vector<std::byte>({std::byte{32}, std::byte{0x21},
                      std::byte{0x43}, std::byte{0x65}, std::byte{0x87}}),
              "both Classic versions echo an optional server challenge exactly");
        auto boot = bootstrap(session, raw);
        const auto generation = session.generation();
        for (const auto bytes : {0U, 80U * 1024U * 1024U + 1}) {
            Packet invalid_start{18};
            invalid_start.integer<std::uint32_t>(bytes);
            check(!session.ingest(invalid_start.bytes).error.empty() &&
                      session.ready() && session.generation() == generation,
                  "invalid map sizes cannot retire the current map");
        }
        Packet invalid_start{18};
        invalid_start.integer<std::uint32_t>(1024);
        for (int tail = 1; tail <= 3; ++tail) {
            invalid_start.byte(0);
            check(!session.ingest(invalid_start.bytes).error.empty(),
                  "reject partial 076 CRC or trailing 075 header bytes");
        }
        const auto wire_equals = [](const ClassicPackets& packets, const Packet& expected) {
            return packets.size() == 1 && packets.front() == expected.bytes;
        };
        Packet native_join{15};
        for (const auto value : {2, 5, 0, 0}) native_join.byte(static_cast<std::uint8_t>(value));
        native_join.text("Wire test");
        Packet expected_join{9};
        for (const auto value : {0, 0, 0, 2}) expected_join.byte(static_cast<std::uint8_t>(value));
        expected_join.integer<std::uint32_t>(0);
        for (int i = 0; i < 3; ++i) expected_join.byte(112);
        expected_join.text("Wire test");
        check(wire_equals(session.translate_client(native_join.bytes), expected_join),
              "both protocols send the original ExistingPlayer join format");
        const auto hit = classic_hit_packet(1, 0);
        check(!session.accepts_client_action(hit), "no gameplay before an authoritative spawn");
        check(session.translate_client(encode_packet(SetColorPacket{0, 0x906030})).empty() &&
                  session.translate_client(encode_packet(WeaponReloadPacket{0, 6, false})).empty(),
              "colour/reload requests cannot precede a confirmed join");
        check(session.ingest(spawn(0).bytes).error.empty(), "stability local spawn");
        Packet team{29}; team.byte(0); team.byte(1);
        check(wire_equals(session.translate_client(std::array{std::byte{77}, std::byte{99}, std::byte{3}}), team),
              "ChangeTeam uses the server-assigned local ID and original team numbering");
        Packet weapon{30}; weapon.byte(0); weapon.byte(1);
        check(wire_equals(session.translate_client(encode_packet(SetClassLoadoutPacket{0, 5, true,
                            {4, 38, 31, 5}, {}, {}})), weapon),
              "SMG loadout emits original ChangeWeapon, never retail loadout bytes");
        for (const auto id : {29, 30}) {
            const auto ignored = session.ingest(std::array{static_cast<std::byte>(id), std::byte{255}, std::byte{255}});
            check(ignored.error.empty() && ignored.events.empty(),
                  "server ChangeTeam/ChangeWeapon notifications never replace authoritative spawn data");
        }
        check(session.translate_client(encode_packet(SetClassLoadoutPacket{0, 5, true,
                  {4, 38, 31, 5}, {}, {}})).empty(),
              "invalid server ChangeWeapon cannot silently reset selected SMG to rifle");
        Packet reload{28}; reload.byte(0); reload.byte(0); reload.byte(0);
        check(wire_equals(session.translate_client(encode_packet(WeaponReloadPacket{0, 38, false})), reload),
              "reload sends the four-byte original request");
        check(session.translate_client(encode_packet(WeaponReloadPacket{0, 38, true})).empty(),
              "client never claims an authoritative reload completion");
        Packet chat{17}; chat.byte(0); chat.byte(1); chat.text("Team test");
        check(wire_equals(session.translate_client(encode_packet(ChatMessagePacket{99, 1, "Team test"})), chat),
              "chat uses the assigned local ID and original team channel");
        Packet grenade{6}; grenade.byte(0); grenade.number(2.5F);
        grenade.vector(32, 33, 50); grenade.vector(1, 0, -0.5F);
        check(classic_grenade_packet(0, 2.5F, {32, 33, 226}, {1, 0, -0.5F}) == grenade.bytes &&
                  session.accepts_client_action(grenade.bytes),
              "grenade positions subtract only the world offset and preserve original velocity units");
        for (const auto& action : {hit, grenade.bytes, classic_block_packet(0, 0, {32, 32, 235}),
                                  classic_line_packet(0, {32, 32, 235}, {35, 32, 235})}) {
            for (std::size_t size = 0; size < action.size(); ++size)
                check(!valid_classic_client_action(std::span{action}.first(size)),
                      "every truncated outgoing action is rejected");
            auto trailing = action; trailing.push_back(std::byte{0});
            check(!valid_classic_client_action(trailing), "outgoing actions reject extra trailing bytes");
        }
        check(session.accepts_client_action(hit), "valid hit identifies its target, not the sender");
        check(session.accepts_client_action(classic_block_packet(0, 0, {32, 32, 235})),
              "valid terrain request from the current local slot");
        check(!session.accepts_client_action(classic_block_packet(1, 0, {32, 32, 235})),
              "wrong-life sender ID cannot reach the server");
        check(!session.accepts_client_action(std::array{std::byte{4}, std::byte{0}, std::byte{1}}),
              "tagging a native packet cannot bypass the Classic action allowlist");

        ClassicMotion motion;
        motion.position = {256.5F, 256.5F, 233.75F};
        motion.alive = true;
        motion.actions = 1;
        const auto first_motion = session.motion(motion, 1);
        check(first_motion.size() == 5, "stability first life sends every original motion field");
        for (const auto& packet : first_motion) {
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            check(((id == 0 || id == 1) && packet.size() == 13) ||
                      ((id == 3 || id == 4 || id == 7) && packet.size() == 3),
                  "both versions send only exact original movement/tool/button layouts");
        }
        Packet death{16};
        death.byte(0);
        death.byte(0);
        death.byte(4);
        death.byte(3);
        check(session.ingest(death.bytes).error.empty(), "stability authoritative death");
        check(!session.accepts_client_action(hit), "queued gameplay is rejected after server death");
        const auto released = session.motion(motion, 1.1);
        check(released.size() == 2 && released[0].back() == std::byte{0} &&
                  released[1].back() == std::byte{0},
              "stale render input releases controls after server death");
        check(session.motion(motion, 2).empty(), "stale render input cannot restart a dead life");
        check(session.ingest(spawn(0).bytes).error.empty(), "stability respawn");
        const auto resumed = session.motion(motion, 2.1);
        check(std::any_of(resumed.begin(), resumed.end(), [](const auto& packet) {
                  return packet.front() == std::byte{1};
              }), "respawn resends aim even when the camera direction has not changed");

        Protocol168TerrainReplica terrain{*boot->map, 1, true, false, true};
        // ZeroSpades uses a temporary builder colour when a plugin sends a
        // block for a synthetic ID rather than a present player.
        Packet palette{8};
        palette.byte(255);
        palette.byte(0x30);
        palette.byte(0x60);
        palette.byte(0x90);
        check(session.ingest(palette.bytes).error.empty(), "synthetic builder palette");
        for (const auto builder : {std::uint8_t{32}, std::uint8_t{255}}) {
          palette.bytes[1] = static_cast<std::byte>(builder);
          check(session.ingest(palette.bytes).error.empty(), "absent or synthetic builder palette");
          for (const int z : {61, 62, 63}) {
            Packet line{14};
            line.byte(builder);
            for (int endpoint = 0; endpoint < 2; ++endpoint) {
                line.integer<std::int32_t>(250);
                line.integer<std::int32_t>(304);
                line.integer<std::int32_t>(z);
            }
            const auto built = session.ingest(line.bytes);
            check(built.error.empty() && built.events.size() == 2,
                  "server-owned block lines may address all 64 layers and synthetic IDs");
            for (const auto& event : built.events)
                check(terrain.apply(event).error.empty(), "plugin block line applies to shared terrain");
            check(terrain.player_color(builder) == 0x906030U,
                  "plugin line retains its temporary builder colour");
            if (z >= 62)
                check(classic_line_packet(0, {250, 304, z + 176}, {250, 304, z + 176}).empty(),
                      "server edit support never relaxes outgoing protected-layer checks");
          }
        }
        Packet server_build{13};
        server_build.byte(255);
        server_build.byte(0);
        for (const int value : {32, 32, 50}) server_build.integer<std::int32_t>(value);
        const auto built = session.ingest(server_build.bytes);
        const auto parsed = decode_terrain_packet(built.events.front());
        check(parsed && std::get<BlockBuildColoredPacket>(*parsed.packet).color == 0x906030U,
              "single plugin blocks share the same temporary palette as lines");
        static_cast<void>(bootstrap(session, raw));
        check(!session.accepts_client_action(hit), "map rotation cannot reuse an old-life request");
    }
}

void classic_weapon_timing_parity_tests() {
    using world::WeaponActionKind;
    for (const auto protocol : {3, 4}) {
        world::WeaponRuntime runtime;
        runtime.set_classic_protocol(static_cast<std::uint8_t>(protocol));
        runtime.replace_loadout(std::array<std::uint8_t, 4>{4, 6, 31, 5}, 6);
        const auto step = [&](double dt = 1.0 / 60.0) { runtime.tick(dt); return runtime.take_actions(); };
        runtime.set_primary(true);
        check(step().size() == 1, "rifle fires immediately when ready");
        check(runtime.select(4) == world::WeaponStateResult::accepted, "select spade after gunfire");
        runtime.set_primary(true);
        const auto spade = step();
        check(spade.size() == 1 && spade[0].kind == WeaponActionKind::melee && !spade[0].secondary,
              "rifle cooldown must not delay a ready spade");
        static_cast<void>(runtime.select(6));
        runtime.set_primary(true);
        check(step().empty(), "switching through a spade cannot shorten the rifle deadline");
        static_cast<void>(runtime.select(5));
        runtime.set_primary(true);
        const auto block = step();
        check(block.size() == 1 && block[0].kind == WeaponActionKind::block_line_begin,
              "gun and spade timers cannot delay a ready block tool");
        static_cast<void>(runtime.select(31));
        runtime.set_primary(true);
        const auto primed = step();
        check(primed.size() == 1 && primed[0].kind == WeaponActionKind::throwable_primed,
              "block timer cannot delay a grenade");
        runtime.set_primary(false);
        const auto thrown = step();
        check(thrown.size() == 1 && thrown[0].kind == WeaponActionKind::oriented_item, "release cooked grenade");
        static_cast<void>(runtime.select(4));
        static_cast<void>(step());
        static_cast<void>(runtime.select(31));
        runtime.set_primary(true);
        check(step().empty(), "switching tools preserves grenade cooldown");
        runtime.set_primary(false);
        for (int i = 0; i < 40; ++i) static_cast<void>(step());
        runtime.set_primary(true);
        const auto primed_again = step();
        check(primed_again.size() == 1 && primed_again[0].kind == WeaponActionKind::throwable_primed,
              "inactive tool cooldown expires on the common simulation clock");
        runtime.on_unset();
        static_cast<void>(runtime.select(6));
        runtime.set_primary(true);
        check(step().size() == 1, "new life clears old tool deadlines");

        // Reference trace from ZeroSpades Weapon::FrameNext: a 100ms gun
        // updated every 60ms preserves its old deadline instead of adding a
        // fresh interval after a late frame. One shot maximum per update.
        world::WeaponRuntime cadence;
        cadence.set_classic_protocol(static_cast<std::uint8_t>(protocol));
        cadence.replace_loadout(std::array<std::uint8_t, 1>{38}, 38);
        cadence.set_primary(true);
        std::vector<int> fired;
        for (int frame = 0; frame < 10; ++frame) {
            cadence.tick(0.06);
            const auto shots = cadence.take_actions();
            check(shots.size() <= 1, "a delayed frame never emits a burst of catch-up hits");
            if (!shots.empty()) fired.push_back(frame);
        }
        check(fired == std::vector<int>({0, 2, 4, 5, 7, 9}), "SMG uneven-frame cadence matches ZeroSpades");
        cadence.set_primary(false);
        cadence.tick(0.25);
        cadence.set_primary(true);
        cadence.tick(0.01);
        check(cadence.take_actions().size() == 1, "new trigger starts without a backlog of missed shots");

        world::WeaponRuntime digger;
        digger.set_classic_protocol(static_cast<std::uint8_t>(protocol));
        digger.replace_loadout(std::array<std::uint8_t, 1>{4}, 4);
        const auto dig_frames = [&](int frames) {
            for (int n = 0; n < frames; ++n) digger.tick(0.05);
            return digger.take_actions();
        };
        digger.set_secondary(true);
        check(dig_frames(18).empty(), "initial partial dig charge");
        digger.set_secondary(false);
        digger.set_secondary(true);
        check(dig_frames(14).empty(), "release and re-press between ticks resets dig charge");
        check(dig_frames(8).size() == 1, "re-pressed dig requires a full charge");
        digger.cancel_interaction();
        digger.set_secondary(true);
        check(dig_frames(14).empty(), "input cancellation cannot carry the previous dig charge");
        check(dig_frames(8).size() == 1, "digging resumes normally after input cancellation");
    }
}

void classic_spread_stream_tests(const std::vector<std::byte>& raw) {
    const auto loaded = network::load_classic_vxl(raw);
    check(static_cast<bool>(loaded), "spread fixture VXL");
    const auto& map = *loaded.map;
    world::PlayerMovementState player;
    player.position = {250, 250, 200};
    player.orientation = {1, 0, 0};
    const std::array<std::uint64_t, 2> seed{0xABCD0123456789ULL, 0x987654321ABCDEFULL};
    world::ClassicCombat combat{seed}, replay{seed};
    world::WeaponAction shot{world::WeaponActionKind::hitscan, 38, 7, 1};
    std::set<std::array<double, 3>> directions;
    double sum{}, squared{};
    constexpr int samples = 8192;
    for (int n = 0; n < samples; ++n) {
        const auto actual = combat.attack(map, player, {}, shot, 3, false, 0);
        check(actual.tracers.size() == 1, "one SMG pellet per shot");
        const auto ray = actual.tracers[0].direction;
        directions.insert({ray.x, ray.y, ray.z});
        sum += ray.y + ray.z;
        squared += ray.y * ray.y + ray.z * ray.z;
        if (n < 32) {
            auto changed_visual_seed = shot;
            changed_visual_seed.seed = static_cast<std::uint8_t>(n);
            const auto expected = replay.attack(map, player, {}, changed_visual_seed, 3, false, 0).tracers[0].direction;
            check(ray.x == expected.x && ray.y == expected.y && ray.z == expected.z,
                  "retail byte seed cannot control or repeat the Classic spread stream");
        }
        if (n == 15) { combat.reset(); replay.reset(); }
    }
    check(directions.size() > 8000, "continuous Classic spread has more than 255 possible directions");
    // Difference of two uniform integers [0,32767], scaled as ZeroSpades.
    // Unit-vector normalization slightly reduces the variance at this spread.
    constexpr double reference_variance = 2.0 * (32767.0 * 32769.0 / 12.0) /
                                          (16383.0 * 16383.0) * 0.012 * 0.012;
    check(std::abs(sum / (samples * 2)) < 0.0004 &&
              std::abs(squared / (samples * 2) / reference_variance - 1.0) < 0.04,
          "spread is centered and has the variance of ZeroSpades integer sampling");
    // Identical random streams isolate stance multipliers from random noise.
    for (const auto protocol : {3, 4}) for (const auto tool : {6, 38, 37}) {
        world::ClassicCombat hip{seed}, aim{seed}, crouch{seed};
        shot.tool_id = static_cast<std::uint8_t>(tool);
        player.crouch = false;
        const auto h = hip.attack(map, player, {}, shot, static_cast<std::uint8_t>(protocol), false, 0);
        const auto a = aim.attack(map, player, {}, shot, static_cast<std::uint8_t>(protocol), true, 0);
        player.crouch = true;
        const auto c = crouch.attack(map, player, {}, shot, static_cast<std::uint8_t>(protocol), false, 0);
        check(h.tracers.size() == (tool == 37 ? 8U : 1U), "original per-version pellet count");
        const auto hy = h.tracers[0].direction.y, ay = a.tracers[0].direction.y, cy = c.tracers[0].direction.y;
        check(std::abs(ay / hy - 0.5) < 0.01, "aiming halves spread in both Classic versions");
        check(std::abs(cy / hy - (tool == 37 ? 1.0 : 0.5)) < 0.01,
              "crouching halves gun spread but never shotgun spread");
    }
}

void shovel_dig_tests(const std::vector<std::byte>& raw) {
    for (const auto protocol : {3, 4}) {
        auto loaded = network::load_classic_vxl(raw);
        check(static_cast<bool>(loaded), "dig fixture VXL");
        auto map = std::make_shared<world::VxlMap>(std::move(*loaded.map));
        for (std::uint32_t z = 231; z <= 235; ++z)
            static_cast<void>(map->set_voxel(252, 250, z, {100, 100, 100, 255}));
        world::TutorialSessionConfig config;
        config.network_authoritative = true;
        config.classic_protocol = static_cast<std::uint8_t>(protocol);
        config.initial_class_id = 5;
        config.initial_loadout = {4, 6, 31, 5};
        config.initial_tool = 4;
        config.initial_position = {250.5, 250.5, 233.75};
        config.initial_orientation = {1, 0, 0};
        world::TutorialWorldSession session{map, config};
        session.set_secondary_held(true);
        for (int frame = 0; frame < 12; ++frame) session.tick();
        session.set_secondary_held(false);
        for (int frame = 0; frame < 60; ++frame) session.tick();
        check(session.take_weapon_actions().empty(), "tapping right-click cancels the charged dig");
        session.set_secondary_held(true);
        for (int frame = 0; frame < 59; ++frame) session.tick();
        check(session.take_weapon_actions().empty(), "dig must charge for one second before removing blocks");
        for (int frame = 0; frame < 3; ++frame) session.tick();
        const auto actions = session.take_weapon_actions();
        check(actions.size() == 1 && actions[0].kind == world::WeaponActionKind::melee && actions[0].secondary,
              "held right-click emits exactly one charged shovel dig");
        world::ClassicCombat combat;
        const auto dig = combat.attack(*map, session.player(), {}, actions[0],
                                      config.classic_protocol, false, 1.1);
        check(dig.destroy.has_value() && dig.block_action == 2 && dig.hits.empty(),
              "secondary shovel chooses three-block terrain action, never player damage");
        const auto cell = *dig.destroy;
        const auto request = network::classic_block_packet(0, dig.block_action,
            {static_cast<std::int32_t>(cell.x), static_cast<std::int32_t>(cell.y), static_cast<std::int32_t>(cell.z)});
        check(request.size() == 15 && request[0] == std::byte{13} && request[2] == std::byte{2},
              "three-block dig uses one original BlockAction packet with SPADE_DESTROY");
        network::ClassicProtocolSession wire{static_cast<network::GameProtocol>(protocol)};
        static_cast<void>(bootstrap(wire, raw));
        const auto reply = wire.ingest(request);
        check(reply.error.empty() && reply.events.size() == 1, "server dig confirmation decodes");
        network::Protocol168TerrainReplica replica{*map, 1, true, false, true};
        const auto removed = replica.apply(reply.events.front());
        check(removed.mutation.accepted && !map->solid(cell.x, cell.y, cell.z - 1) &&
                  !map->solid(cell.x, cell.y, cell.z) && !map->solid(cell.x, cell.y, cell.z + 1) &&
                  map->solid(cell.x, cell.y, 235),
              "confirmed dig removes the vertical three blocks and preserves connected lower terrain");
        session.set_secondary_held(false);
        for (int frame = 0; frame < 90; ++frame) session.tick();
        check(session.take_weapon_actions().empty(), "releasing right-click stops repeated digging");
        session.set_primary_held(true);
        session.set_secondary_held(true);
        check((session.action_flags() & 3U) == 1U, "server sees primary-only shovel input when both buttons are held");
        for (int frame = 0; frame < 90; ++frame) session.tick();
        const auto primary = session.take_weapon_actions();
        check(!primary.empty() && std::ranges::none_of(primary, [](const auto& action) { return action.secondary; }),
              "left-click suppresses digging for the entire combined press");
        session.set_primary_held(false);
        session.tick();
        check((session.action_flags() & 3U) == 2U, "held right-click resumes after primary release");
        for (int frame = 0; frame < 58; ++frame) session.tick();
        check(session.take_weapon_actions().empty(), "resumed right-click must charge again before digging");
        for (int frame = 0; frame < 4; ++frame) session.tick();
        const auto resumed = session.take_weapon_actions();
        check(resumed.size() == 1 && resumed[0].secondary, "full new charge produces one resumed dig");
    }
}

void gameplay_tests(const std::vector<std::byte>& raw) {
    auto loaded = network::load_classic_vxl(raw);
    check(static_cast<bool>(loaded), "fixture VXL");
    auto& map = *loaded.map;
    world::TutorialSessionConfig config;
    config.network_authoritative = true;
    config.classic_protocol = 3;
    config.initial_class_id = 5;
    config.initial_loadout = {4, 6, 31, 5};
    config.initial_tool = 6;
    config.initial_position = {256.5, 256.5, 233.75};
    config.initial_orientation = {1, 0, 0};
    world::TutorialWorldSession interactive{std::make_shared<world::VxlMap>(map), config};
    for (int n = 0; n < 90; ++n)
        interactive.tick();
    check(interactive.selected_ammo()->magazine == 10 && interactive.selected_ammo()->reserve == 50,
          "interactive classic session preserves original rifle ammunition while idle");
    interactive.set_primary_held(true);
    interactive.tick();
    interactive.set_primary_held(false);
    check(std::abs(interactive.pitch() + 2.864788975654116) < 1e-6,
          "interactive 075 rifle applies original 0.05 radian kick, not retail recoil");
    for (const auto protocol : {3, 4}) {
        for (const auto tool : {6, 38, 37}) {
            auto reload_config = config;
            reload_config.classic_protocol = static_cast<std::uint8_t>(protocol);
            reload_config.initial_loadout = {4, static_cast<std::uint8_t>(tool), 31, 5};
            reload_config.initial_tool = static_cast<std::uint8_t>(tool);
            world::TutorialWorldSession reloader{std::make_shared<world::VxlMap>(map), reload_config};
            for (int i = 0; i < 30; ++i) reloader.tick();
            reloader.set_primary_held(true); reloader.tick(); reloader.set_primary_held(false);
            check(reloader.request_reload() == world::WeaponStateResult::accepted, "start animation timing reload");
            check(std::abs(reloader.weapon_reload_progress()) < 1e-8,
                  "custom reload animation starts at zero for each Classic weapon");
            const auto rules = world::classic_weapon_rules(static_cast<std::uint8_t>(protocol), static_cast<std::uint8_t>(tool));
            for (int i = 0; i < static_cast<int>(std::round(rules->reload * 30)); ++i) reloader.tick();
            check(std::abs(reloader.weapon_reload_progress() - .5) < .04,
                  "custom reload reaches its midpoint halfway through the actual protocol reload");
        }
    }
    for (const auto protocol : {3, 4}) {
        for (const auto tool : {6, 38, 37}) {
            const auto rules = world::classic_weapon_rules(static_cast<std::uint8_t>(protocol),
                                                           static_cast<std::uint8_t>(tool));
            const double up = tool == 38 ? 0.0125 : protocol == 4 ? 0.075 : tool == 6 ? 0.05 : 0.1;
            const double side = tool == 38 ? 0.00005 : tool == 6 && protocol == 3 ? 0.0001 : 0.0002;
            const double degrees = 180.0 / std::numbers::pi;
            const auto idle = world::classic_recoil_kick(*rules, 0, false, false, false, false);
            check(std::abs(idle.pitch_degrees + up * degrees) < 1e-10 &&
                      std::abs(idle.yaw_degrees + side * 255.5 * degrees) < 1e-10,
                  "all original weapon recoil magnitudes");
            const auto opposite =
                world::classic_recoil_kick(*rules, 512, false, false, false, false);
            check(std::abs(idle.yaw_degrees + opposite.yaw_degrees) < 1e-10,
                  "original recoil wave changes direction at 512ms");
            const auto walk = world::classic_recoil_kick(*rules, 0, true, false, false, false);
            const auto aimed = world::classic_recoil_kick(*rules, 0, true, true, false, false);
            const auto crouch = world::classic_recoil_kick(*rules, 0, false, false, true, false);
            const auto air = world::classic_recoil_kick(*rules, 0, true, false, true, true);
            check(walk.pitch_degrees == idle.pitch_degrees * 2 &&
                      aimed.pitch_degrees == idle.pitch_degrees &&
                      crouch.pitch_degrees == idle.pitch_degrees / 2 &&
                      air.pitch_degrees == idle.pitch_degrees * 4,
                  "original walking, aimed, crouched and airborne recoil multipliers");
        }
    }
    world::PlayerMovementState p;
    p.position = {256.5, 256.5, 233.75};
    p.orientation = {1, 0, 0};
    bool jump{};
    std::ifstream stream{CLASSIC_GOLDEN};
    nlohmann::json golden;
    stream >> golden;
    for (const auto& row : golden) {
        const auto flags = row[0].get<unsigned>();
        world::PlayerInputState input;
        input.forward = flags & 1;
        input.backward = flags & 2;
        input.left = flags & 4;
        input.right = flags & 8;
        input.jump = flags & 16;
        input.crouch = flags & 32;
        input.sneak = flags & 64;
        input.sprint = flags & 128;
        static_cast<void>(
            world::step_classic_player(p, input, &map, 1.0 / 60.0, row[1].get<int>() != 0, jump));
        check(std::abs(p.position.x - row[2].get<double>()) < 0.0001 &&
                  std::abs(p.position.y - row[3].get<double>()) < 0.0001 &&
                  std::abs(p.position.z - 176 - row[4].get<double>()) < 0.0001,
              "movement matches piqueserver golden");
    }
    world::PlayerInventory inventory;
    inventory.set_classic_protocol(3);
    check(inventory.spawn_with_loadout(5, std::array<std::uint8_t, 4>{4, 38, 31, 5}),
          "classic inventory");
    check(inventory.blocks() == 50 && inventory.maximum_blocks() == 50, "classic wallet");
    check(inventory.ammo(38)->magazine == 30 && inventory.ammo(38)->reserve == 120 &&
              inventory.ammo(31)->magazine == 3,
          "075 ammunition");
    auto& weapon = inventory.weapons();
    static_cast<void>(weapon.select(5));
    weapon.set_custom(true);
    weapon.tick(1.0 / 60);
    const auto picked = weapon.take_actions();
    check(picked.size() == 1 && picked.front().kind == world::WeaponActionKind::color_pick,
          "classic blocks preserve the native eyedropper action");
    weapon.set_custom(false);
    static_cast<void>(weapon.select(38));
    weapon.tick(0.5);
    weapon.set_primary(true);
    weapon.tick(1.0 / 60);
    check(weapon.take_actions().size() == 1 && inventory.ammo(38)->magazine == 29,
          "single classic round");
    weapon.set_primary(false);
    check(weapon.request_reload() == world::WeaponStateResult::accepted, "request classic reload");
    static_cast<void>(weapon.select(4));
    static_cast<void>(weapon.select(38));
    check(inventory.ammo(38)->reloading, "switching tools cannot bypass server reload");
    for (int n = 0; n < 240; ++n)
        weapon.tick(1.0 / 60);
    check(inventory.ammo(38)->magazine == 29 && inventory.ammo(38)->reloading,
          "reload awaits server ammo");
    weapon.classic_reload_completed(30, 119);
    check(inventory.ammo(38)->magazine == 30 && inventory.ammo(38)->reserve == 119 &&
              !inventory.ammo(38)->reloading,
          "reload uses exact server counters");
    world::WeaponReplicationState native;
    native.replace_loadout(std::array<std::uint8_t, 1>{38});
    check(native.ammo(38)->reserve != 120, "Classic+ ammo table unchanged");
    world::ClassicCombat combat{std::array<std::uint64_t, 2>{12345, 67890}};
    p.position = {250, 250, 232};
    p.orientation = {1, 0, 0};
    p.crouch = false;
    const std::array targets{world::ClassicHitTarget{1, {260, 250, 232}, {-1, 0, 0}, false, false}};
    world::WeaponAction shot{world::WeaponActionKind::hitscan, 6, 7, 1};
    auto hit = combat.attack(map, p, targets, shot, 3, true, 0);
    check(hit.hits.size() == 1 && hit.hits[0].part == 1, "original head hitbox");
    check(hit.hits.front().position.x > 259 && hit.hits.front().position.x < 261,
          "hit feedback has the actual hitbox contact position");
    // Observer fire uses these same contacts, including shotgun pellets, but
    // consumes only hits/tracers/impacts and never applies damage requests.
    const std::array observer_targets{
        world::ClassicHitTarget{3, {270, 250, 232}, {-1, 0, 0}, false, false},
        targets.front()};
    world::ClassicCombat observer{std::array<std::uint64_t, 2>{12345, 67890}};
    const auto observed = observer.attack(map, p, observer_targets, shot, 3, true, 0);
    check(observed.hits.size() == 1 && observed.hits.front().player == 1 &&
              observed.tracers.front().endpoint.x == observed.hits.front().position.x,
          "observer feedback stops on the closest player instead of bleeding through players");
    const auto buckshot = observer.attack(map, p, observer_targets,
        {world::WeaponActionKind::hitscan, 37, 7, 1}, 3, true, 0);
    check(buckshot.tracers.size() == 8 && !buckshot.hits.empty(),
          "remote shotgun keeps every tracer and produces player contacts");
    for (const auto tool : std::array<std::uint8_t, 3>{6, 38, 37}) {
        const std::array<std::uint8_t, 4> loadout{4, tool, 31, 5};
        check(world::classic_kill_tool(0, loadout) == tool &&
                  world::classic_kill_tool(2, loadout) == 4 &&
                  !world::classic_kill_tool(22, loadout),
              "firearm kills retain the gun icon even after switching to the spade");
    }
    for (unsigned y = 249; y < 252; ++y)
        for (unsigned z = 230; z < 235; ++z)
            static_cast<void>(map.set_voxel(255, y, z, {100, 100, 100, 255}));
    check(combat.attack(map, p, targets, shot, 3, true, 1).hits.empty(), "terrain occludes hit");
    const auto wall_color = map.color(255, 250, 232);
    const auto occluded = observer.attack(map, p, observer_targets, shot, 3, true, 1);
    check(occluded.hits.empty() && !occluded.impacts.empty() &&
              map.color(255, 250, 232) == wall_color && !map.damaged_block(255, 250, 232),
          "observer fire shows terrain contact without blood through walls or map mutation");
    static_cast<void>(map.set_voxel(200, 200, 234, {100, 100, 100, 255}));
    static_cast<void>(map.set_voxel(200, 200, 235, {100, 100, 100, 255}));
    network::Protocol168TerrainReplica replica{map, 1, true, false, true};
    Packet remove{252};
    remove.byte(1);
    remove.byte(0);
    remove.integer<std::int32_t>(200);
    remove.integer<std::int32_t>(200);
    remove.integer<std::int32_t>(59);
    auto result = replica.apply(remove.bytes);
    check(result.mutation.accepted && !map.solid(200, 200, 234) && !map.solid(200, 200, 235),
          "classic face-connected collapse");
    const auto falling = replica.take_falling_components();
    check(falling.size() == 1 && falling.front().size() == 1 &&
              falling.front().front().cell.z == 234,
          "classic collapse passes detached structure to the existing falling renderer");
    check(!replica.take_impact_events().empty(),
          "confirmed legacy removal emits native block debris");
    combat.reset();
    p.position = {253, 250.5, 232.5};
    p.orientation = {1, 0, 0};
    const auto damaged = combat.attack(map, p, {}, shot, 3, true, 5);
    check(damaged.damage.size() == 1 && damaged.impacts.size() == 1 && !damaged.destroy,
          "first rifle hit emits damage shading and impact feedback without removing terrain");
    const auto damage = damaged.damage.front();
    replica.set_classic_block_damage(damage.cell, damage.remaining, damage.original);
    check(map.solid(damage.cell.x, damage.cell.y, damage.cell.z) &&
              map.color(damage.cell.x, damage.cell.y, damage.cell.z)->red < damage.original.red &&
              !replica.take_dirty_chunks().empty(),
          "legacy hits darken the existing mesh and enqueue its remesh");
    for (const auto& healed : combat.expire(16))
        replica.set_classic_block_damage(healed.cell, healed.remaining, healed.original);
    check(map.color(damage.cell.x, damage.cell.y, damage.cell.z)->red == damage.original.red,
          "expired legacy damage restores the original block color");
    combat.reset();
    const auto pellets =
        combat.attack(map, p, {}, {world::WeaponActionKind::hitscan, 37, 7, 1}, 3, true, 20);
    check(pellets.destroy && pellets.tracers.size() == 8 && pellets.impacts.size() == 8,
          "all eight shotgun pellets remain visible after one requests a block destruction");
    world::TutorialSessionConfig spade_config = config;
    spade_config.initial_tool = 4;
    world::TutorialWorldSession shovel{std::make_shared<world::VxlMap>(map), spade_config};
    shovel.set_primary_held(true);
    shovel.tick();
    shovel.set_primary_held(false);
    const auto first_pose =
        world::evaluate_retail_tool_animation(4, shovel.seconds_since_weapon_animation(), true);
    shovel.tick();
    const auto next_pose =
        world::evaluate_retail_tool_animation(4, shovel.seconds_since_weapon_animation(), true);
    check(first_pose.position.y * next_pose.position.y >= 0,
          "legacy shovel does not flip animation direction after its first frame");
    for (int i = 0; i < 13; ++i)
        shovel.tick();
    const auto rest =
        world::evaluate_retail_tool_animation(4, shovel.seconds_since_weapon_animation(), true);
    check(rest.orientation_degrees.x == 0 && rest.position.z == 0,
          "native shovel swing finishes within the original primary cadence");
    auto grenade_config = config;
    grenade_config.initial_tool = 31;
    world::TutorialWorldSession grenadier{std::make_shared<world::VxlMap>(map), grenade_config};
    grenadier.set_primary_held(true);
    for (int i = 0; i < 30; ++i)
        grenadier.tick();
    check(grenadier.seconds_since_weapon_animation() < 1,
          "grenade pullback follows the held interaction clock");
    grenadier.set_primary_held(false);
    grenadier.tick();
    check(grenadier.seconds_since_weapon_animation() > 1000,
          "releasing the grenade does not restart the hand pullback animation");
}

void grenade_tests(const std::vector<std::byte>& raw) {
    std::ifstream stream{std::filesystem::path{CLASSIC_GOLDEN}.parent_path() /
                         "classic-grenades.json"};
    nlohmann::json scenarios;
    stream >> scenarios;
    for (const auto& scenario : scenarios) {
        auto loaded = network::load_classic_vxl(raw);
        auto& map = *loaded.map;
        for (const auto& cell : scenario["added"])
            static_cast<void>(map.set_voxel(
                cell[0], cell[1], cell[2].get<unsigned>() + 176, {100, 100, 100, 255}));
        for (const auto& cell : scenario["removed"])
            static_cast<void>(map.clear_voxel(cell[0], cell[1], cell[2].get<unsigned>() + 176));
        world::Vec3 position{scenario["position"][0],
                             scenario["position"][1],
                             scenario["position"][2].get<double>() + 176};
        world::Vec3 velocity{scenario["velocity"][0].get<double>() * 32,
                             scenario["velocity"][1].get<double>() * 32,
                             scenario["velocity"][2].get<double>() * 32};
        unsigned frame{};
        for (const auto& row : scenario["rows"]) {
            static_cast<void>(world::step_classic_grenade(position, velocity, map, 1.0 / 60));
            const std::array actual{position.x,
                                    position.y,
                                    position.z - 176,
                                    velocity.x / 32,
                                    velocity.y / 32,
                                    velocity.z / 32};
            for (unsigned i = 0; i < actual.size(); ++i)
                if (std::abs(actual[i] - row[i].get<double>()) > 0.0001)
                    throw std::runtime_error("grenade " + scenario["name"].get<std::string>() +
                                             " differs from piqueserver at frame " +
                                             std::to_string(frame));
            ++frame;
        }
    }
}

void water_movement_tests(const std::vector<std::byte>& raw) {
    std::ifstream stream{std::filesystem::path{CLASSIC_GOLDEN}.parent_path() /
                         "classic-movement-water.json"};
    nlohmann::json scenarios;
    stream >> scenarios;
    for (const auto& scenario : scenarios) {
        auto loaded = network::load_classic_vxl(raw);
        auto& map = *loaded.map;
        for (const auto& c : scenario["removed"])
            static_cast<void>(map.clear_voxel(c[0], c[1], c[2].get<unsigned>() + 176));
        for (const auto& c : scenario["added"])
            static_cast<void>(
                map.set_voxel(c[0], c[1], c[2].get<unsigned>() + 176, {100, 100, 100, 255}));
        world::PlayerMovementState p;
        p.position = {scenario["initial"][0],
                      scenario["initial"][1],
                      scenario["initial"][2].get<double>() + 176};
        p.orientation = {1, 0, 0};
        bool jump_held{};
        unsigned frame{}, jumps{};
        for (const auto& row : scenario["rows"]) {
            world::PlayerInputState input;
            input.forward = (row[0].get<unsigned>() & 1) != 0;
            input.jump = (row[0].get<unsigned>() & 16) != 0;
            const auto step =
                world::step_classic_player(p, input, &map, 1.0 / 60, false, jump_held);
            jumps += step.jumped;
            if (std::abs(p.position.x - row[1].get<double>()) > 0.0001 ||
                std::abs(p.position.y - row[2].get<double>()) > 0.0001 ||
                std::abs(p.position.z - 176 - row[3].get<double>()) > 0.0001 ||
                std::abs(p.velocity.z - row[6].get<double>()) > 0.0001 ||
                p.airborne != (row[7].get<int>() != 0) || p.wade != (row[8].get<int>() != 0) ||
                step.jumped != (row[9].get<int>() != 0))
                throw std::runtime_error(
                    scenario["name"].get<std::string>() + " differs from piqueserver at frame " +
                    std::to_string(frame) + " actual=" + std::to_string(p.position.x) + "," +
                    std::to_string(p.position.z - 176) + "," + std::to_string(p.velocity.z) +
                    " air=" + std::to_string(p.airborne) + " wade=" + std::to_string(p.wade) +
                    " expected=" + row.dump());
            ++frame;
        }
        if (scenario["name"] == "held_jump")
            check(jumps == 1, "held SPACE does not auto bunny-hop in legacy mode");
        if (scenario["name"] == "water_exit")
            check(p.position.x > 258 && p.position.z < 236, "can jump out of water onto the shore");
    }
    world::TutorialSessionConfig config;
    config.network_authoritative = true;
    config.classic_protocol = 3;
    config.initial_position = {250.5, 250.5, 233.75};
    config.initial_orientation = {1, 0, 0};
    auto loaded = network::load_classic_vxl(raw);
    world::TutorialWorldSession session{std::make_shared<world::VxlMap>(std::move(*loaded.map)),
                                        config};
    for (int n = 0; n < 30; ++n)
        session.tick();
    session.set_action_held(world::TutorialAction::jump, true);
    session.tick();
    check((session.movement_flags() & 16U) != 0, "session sends the accepted jump edge");
    for (int n = 0; n < 180; ++n) {
        session.tick();
        check((session.movement_flags() & 16U) == 0, "held legacy jump never repeats on the wire");
    }

    auto& map = session.map();
    world::PlayerMovementState builder;
    builder.position = {250.5, 250.5, 233.75};
    builder.orientation = {0, 0, 1};
    check(!world::classic_build_target(map, builder, {}),
          "cannot place a block inside standing feet");
    builder.position.z = 232.7;
    const auto below = world::classic_build_target(map, builder, {});
    check(below && below->z == 235, "can place below feet after jumping clear of the block");
}
} // namespace
int main() {
    try {
        const auto raw = flat_map();
        protocol_tests(raw);
        territory_state_padding_tests(raw);
        objective_tests(raw);
        stabilization_tests(raw);
        gameplay_tests(raw);
        shovel_dig_tests(raw);
        classic_weapon_timing_parity_tests();
        classic_spread_stream_tests(raw);
        grenade_tests(raw);
        water_movement_tests(raw);
        std::cout << "Classic codec, movement, weapons, combat and naming passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
