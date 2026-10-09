#include "battlespades/network/classic_protocol.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "classic_text.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace battlespades::network {
namespace {
// Wire layouts verified against ZeroSpades NetProtocol/NetClient and
// piqueserver pyspades/contained.pyx. See docs/CLASSIC_PROTOCOL.md.
struct Reader {
    std::span<const std::byte> data;
    std::size_t pos{};
    std::size_t remaining() const {
        return data.size() - pos;
    }
    void require(std::size_t n) const {
        if (n > remaining())
            throw std::runtime_error("Truncated classic packet");
    }
    std::uint8_t byte() {
        require(1);
        return std::to_integer<std::uint8_t>(data[pos++]);
    }
    template <class T> T integer() {
        require(sizeof(T));
        std::uint64_t n{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            n |= std::uint64_t{byte()} << (i * 8U);
        return static_cast<T>(n);
    }
    float number() {
        const auto f = std::bit_cast<float>(integer<std::uint32_t>());
        if (!std::isfinite(f))
            throw std::runtime_error("Non-finite classic coordinate");
        return f;
    }
    std::array<float, 3> vector(bool position = false) {
        std::array<float, 3> v{number(), number(), number()};
        for (auto f : v)
            if (std::abs(f) > 4096)
                throw std::runtime_error("Classic coordinate out of bounds");
        if (position)
            v[2] += classic_z_offset;
        return v;
    }
    std::optional<std::array<float, 3>> objective_position() {
        std::array<float, 3> v;
        for (auto& f : v)
            f = std::bit_cast<float>(integer<std::uint32_t>());
        // piqueserver's Babel/OneCTF and other scripts deliberately hide
        // objectives at HIDE_POS. Keep that sentinel out of shared render data.
        constexpr auto infinity = std::numeric_limits<float>::infinity();
        if (v == std::array{infinity, infinity, 128.0F})
            return std::nullopt;
        for (auto f : v) {
            if (!std::isfinite(f))
                throw std::runtime_error("Non-finite classic objective coordinate");
            if (std::abs(f) > 4096)
                throw std::runtime_error("Classic objective coordinate out of bounds");
        }
        v[2] += classic_z_offset;
        return v;
    }
    std::array<std::uint8_t, 3> color() {
        const auto b = byte(), g = byte(), r = byte();
        return {r, g, b};
    }
    std::string string(std::size_t count, bool classic = true) {
        require(count);
        std::string s;
        bool ended{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto c = byte();
            if (!c)
                ended = true;
            if (!ended)
                s.push_back(static_cast<char>(c));
        }
        return classic ? classic_decode_text(s) : s;
    }
    void end() const {
        if (remaining())
            throw std::runtime_error("Trailing classic packet bytes");
    }
};
struct Writer {
    std::vector<std::byte> data;
    explicit Writer(std::uint8_t id, std::size_t capacity = 1) {
        data.reserve(capacity);
        byte(id);
    }
    void byte(std::uint8_t v) {
        data.push_back(static_cast<std::byte>(v));
    }
    template <class T> void integer(T v) {
        const auto n = static_cast<std::uint64_t>(v);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            byte(static_cast<std::uint8_t>(n >> (i * 8U)));
    }
    void number(float f) {
        integer(std::bit_cast<std::uint32_t>(f));
    }
    void vector(std::array<float, 3> v, bool position = false) {
        if (position)
            v[2] -= classic_z_offset;
        for (auto f : v)
            number(f);
    }
    void text(std::string_view s, std::size_t max = 90) {
        for (auto c : s.substr(0, max))
            byte(static_cast<std::uint8_t>(c));
        byte(0);
    }
};
std::uint8_t team_in(std::uint8_t t) {
    if (t == 255)
        return 0;
    if (t > 2)
        throw std::runtime_error("Invalid classic team");
    return t == 2 ? 1 : static_cast<std::uint8_t>(t + 2);
}
std::uint8_t team_out(std::uint8_t t) {
    return t == 2 ? 0 : t == 3 ? 1 : 255;
}
std::uint8_t tool_in(std::uint8_t t, std::uint8_t weapon) {
    switch (t) {
    case 0:
        return 4;
    case 1:
        return 5;
    case 2:
        return classic_weapon_tool(weapon);
    case 3:
        return 31;
    default:
        throw std::runtime_error("Invalid classic tool");
    }
}
std::optional<std::uint8_t> tool_out(std::uint8_t t) {
    if (t == 4)
        return 0;
    if (t == 5)
        return 1;
    if (classic_weapon_id(t))
        return 2;
    if (t == 31)
        return 3;
    return {};
}
std::vector<std::uint8_t> loadout(std::uint8_t weapon) {
    return {4, classic_weapon_tool(weapon), 31, 5};
}
std::vector<std::byte> inflate_map(std::span<const std::byte> input) {
    if (input.empty() || input.size() > 16U * 1024U * 1024U)
        throw std::runtime_error("Classic compressed map exceeds limit");
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit(&stream) != Z_OK)
        throw std::runtime_error("Cannot initialize classic map decoder");
    struct Cleanup {
        z_stream& stream;
        ~Cleanup() {
            inflateEnd(&stream);
        }
    } cleanup{stream};
    std::vector<std::byte> result;
    std::array<std::byte, 65536> chunk{};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const auto size = chunk.size() - stream.avail_out;
        if (result.size() + size > 80U * 1024U * 1024U)
            throw std::runtime_error("Classic decompressed map exceeds limit");
        result.insert(
            result.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(size));
    }
    if (status != Z_STREAM_END || stream.avail_in)
        throw std::runtime_error("Invalid classic zlib map stream");
    return result;
}
template <class T> void event(ClassicIngest& out, const T& p) {
    if constexpr (requires { encode_packet(p); })
        out.events.push_back(encode_packet(p));
    else {
        Writer w{T::id};
        const auto fixed = [&](float value) {
            w.integer<std::int16_t>(
                static_cast<std::int16_t>(std::clamp(value * 64.0F, -32768.0F, 32767.0F)));
        };
        const auto color = [&](auto rgb) {
            w.byte(rgb[2]);
            w.byte(rgb[1]);
            w.byte(rgb[0]);
        };
        if constexpr (std::is_same_v<T, MinimapZonePacket>) {
            w.byte(p.key);
            color(p.color);
            for (auto v : p.minimum)
                w.integer(v);
            for (auto v : p.maximum)
                w.integer(v);
            fixed(p.icon_scale);
            w.byte(p.icon_id);
            w.byte(p.locked_in_zone ? 1 : 0);
        } else if constexpr (std::is_same_v<T, MinimapZoneClearPacket>) {
            for (auto v : p.minimum)
                w.integer(v);
            for (auto v : p.maximum)
                w.integer(v);
        } else if constexpr (std::is_same_v<T, MinimapBillboardPacket>) {
            w.integer(p.entity_id);
            w.byte(p.key);
            color(p.color);
            for (auto f : p.position)
                fixed(f);
            w.text(p.icon_name, 96);
            w.byte(p.tracking ? 1 : 0);
        } else if constexpr (std::is_same_v<T, MinimapBillboardClearPacket>)
            w.integer(p.entity_id);
        else if constexpr (std::is_same_v<T, DestroyEntityPacket>)
            w.integer(p.entity_id);
        else if constexpr (std::is_same_v<T, CreateEntityPacket>) {
            w.integer(p.entity_id);
            w.byte(p.type);
            w.byte(p.state);
            w.byte(p.player_id);
            for (auto f : p.position)
                fixed(f);
            for (auto f : p.velocity)
                fixed(f);
            fixed(p.yaw);
            for (auto f : p.color)
                fixed(f);
            fixed(p.radius);
            w.byte(p.face);
            fixed(p.fuse);
            w.byte(0);
            w.byte(0);
            w.byte(p.ugc_mode);
        } else if constexpr (std::is_same_v<T, SetHpPacket>) {
            w.byte(p.health);
            w.byte(p.damage_type);
            for (auto f : p.source)
                fixed(f);
        } else if constexpr (std::is_same_v<T, PlayerLeftPacket>)
            w.byte(p.player_id);
        else if constexpr (std::is_same_v<T, PickPickupPacket>) {
            w.byte(p.player_id);
            w.byte(p.pickup_id);
            w.byte(p.burdensome ? 1 : 0);
        } else if constexpr (std::is_same_v<T, FogColorPacket>) {
            w.byte(0);
            color(p.color);
        } else if constexpr (std::is_same_v<T, SetScorePacket>) {
            w.byte(p.type);
            w.byte(p.reason);
            w.byte(p.specifier);
            w.integer(p.value);
        } else if constexpr (std::is_same_v<T, KillActionPacket>) {
            w.byte(p.player_id);
            w.byte(p.killer_id);
            w.byte(p.kill_type);
            w.byte(p.respawn_time);
            w.byte(p.kill_count);
            w.byte(p.domination ? 1 : 0);
            w.byte(p.revenge ? 1 : 0);
        } else if constexpr (std::is_same_v<T, TerritoryBaseStatePacket>) {
            w.byte(p.base_index);
            w.byte(p.action);
            w.byte(p.controlled_by);
            w.byte(p.attacked_by);
            fixed(p.capture_amount);
        } else
            static_assert(sizeof(T) == 0, "Classic bridge event has no encoder");
        out.events.push_back(std::move(w.data));
    }
}
} // namespace

std::uint8_t classic_weapon_tool(std::uint8_t weapon) noexcept {
    constexpr std::array<std::uint8_t, 3> ids{6, 38, 37};
    return weapon < 3 ? ids[weapon] : 6;
}
std::optional<std::uint8_t> classic_weapon_id(std::uint8_t tool) noexcept {
    if (tool == 6)
        return 0;
    if (tool == 38)
        return 1;
    if (tool == 37)
        return 2;
    return {};
}
std::vector<std::byte> classic_hit_packet(std::uint8_t victim, std::uint8_t part) {
    if (victim >= 128 || part > 4)
        return {};
    return {std::byte{5}, static_cast<std::byte>(victim), static_cast<std::byte>(part)};
}
std::vector<std::byte>
classic_block_packet(std::uint8_t player, std::uint8_t action, std::array<std::int32_t, 3> cell) {
    if (player >= 128 || action > 2 || cell[0] < 0 || cell[0] >= 512 || cell[1] < 0 || cell[1] >= 512 ||
        cell[2] < 176 || cell[2] >= 238)
        return {};
    Writer w{13};
    w.byte(player);
    w.byte(action);
    cell[2] -= 176;
    for (auto v : cell)
        w.integer(v);
    return std::move(w.data);
}
std::vector<std::byte> classic_line_packet(std::uint8_t player,
                                           std::array<std::int32_t, 3> start,
                                           std::array<std::int32_t, 3> end) {
    if (classic_block_packet(player, 0, start).empty() ||
        classic_block_packet(player, 0, end).empty())
        return {};
    Writer w{14};
    w.byte(player);
    start[2] -= 176;
    end[2] -= 176;
    for (auto v : start)
        w.integer(v);
    for (auto v : end)
        w.integer(v);
    return std::move(w.data);
}
std::vector<std::byte> classic_grenade_packet(std::uint8_t player,
                                              float fuse,
                                              std::array<float, 3> position,
                                              std::array<float, 3> velocity) {
    if (player >= 128 || !std::isfinite(fuse) || fuse < 0 || fuse > 3)
        return {};
    for (auto v : position)
        if (!std::isfinite(v) || std::abs(v) > 4096)
            return {};
    for (auto v : velocity)
        if (!std::isfinite(v) || std::abs(v) > 4096)
            return {};
    Writer w{6};
    w.byte(player);
    w.number(fuse);
    w.vector(position, true);
    w.vector(velocity);
    return std::move(w.data);
}
std::optional<std::array<float, 3>> classic_correction(std::span<const std::byte> packet) {
    try {
        Reader r{packet};
        if (r.byte() != 254 || r.byte() > 1)
            return {};
        auto v = r.vector();
        r.end();
        return v;
    } catch (const std::exception&) {
        return {};
    }
}
bool valid_classic_client_action(std::span<const std::byte> packet) noexcept {
    try {
        Reader r{packet};
        const auto kind = r.byte();
        if (r.byte() >= 128)
            return false;
        if (kind == 5) {
            if (r.byte() > 4)
                return false;
        } else if (kind == 6) {
            const auto fuse = r.number();
            if (fuse < 0 || fuse > 3)
                return false;
            static_cast<void>(r.vector());
            static_cast<void>(r.vector());
        } else if (kind == 13 || kind == 14) {
            if (kind == 13 && r.byte() > 2)
                return false;
            for (int end = 0; end < (kind == 14 ? 2 : 1); ++end)
                for (int axis = 0; axis < 3; ++axis) {
                    const auto value = r.integer<std::int32_t>();
                    if (value < 0 || value >= (axis == 2 ? 62 : 512))
                        return false;
                }
        } else
            return false;
        r.end();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
world::VxlLoadResult load_classic_vxl(std::span<const std::byte> raw) {
    return world::VxlMap::load(raw, world::VxlDecodeProfile::classic64);
}

ClassicProtocolSession::ClassicProtocolSession(GameProtocol protocol, std::string name)
    : protocol_(protocol), name_(std::move(name)) {
    if (!is_classic_protocol(protocol))
        throw std::invalid_argument("Expected classic protocol");
}

ClassicIngest ClassicProtocolSession::ingest(std::span<const std::byte> packet) {
    ClassicIngest out;
    try {
        Reader r{packet};
        const auto id = r.byte();
        if (id == 31) {
            // The server-to-client challenge also appears on 0.76 forks.
            // Its direction distinguishes it from our 0.76 MapCached reply.
            Writer w{32};
            w.integer(r.integer<std::uint32_t>());
            r.end();
            out.wire.push_back(std::move(w.data));
        } else if (id == 33) {
            Writer w{34};
            w.byte('b');
            w.byte(0);
            w.byte(1);
            w.byte(0);
            w.text("BattleSpades", 40);
            out.wire.push_back(std::move(w.data));
        } else if (id == 60) {
            const auto count = r.byte();
            r.require(static_cast<std::size_t>(count) * 2);
            r.pos += static_cast<std::size_t>(count) * 2;
            r.end();
            out.wire.push_back({std::byte{60}, std::byte{0}});
        } else if (id == 18) {
            const auto map_size = r.integer<std::uint32_t>();
            if (map_size == 0 || map_size > 80U * 1024U * 1024U)
                throw std::runtime_error("Invalid classic MapStart");
            std::string map_name;
            if (protocol_ == GameProtocol::classic076 && r.remaining()) {
                // Original 0.76 adds CRC32 and a CP437 map name. The CRC is
                // cache metadata: we always request fresh bytes. Five-byte
                // headers also occur in compatible forks and recorded demos.
                static_cast<void>(r.integer<std::uint32_t>());
                if (r.remaining() > 256)
                    throw std::runtime_error("Classic map name exceeds limit");
                map_name = r.string(r.remaining());
            }
            r.end();
            map_size_ = map_size;
            map_name_ = std::move(map_name);
            compressed_.clear();
            deferred_.clear();
            deferred_bytes_ = 0;
            players_ = {};
            temporary_block_color_ = 0x707070U;
            ready_ = false;
            receiving_ = true;
            joined_ = false;
            last_motion_.reset();
            jump_pulse_until_ = 0;
            last_sent_orientation_.reset();
            last_position_seconds_ = last_orientation_seconds_ = 0;
            ++generation_;
            world_loop_ = 0;
            carriers_ = {255, 255};
            objectives_ = {};
            objective_zones_ = {};
            territories_ = {};
            territory_count_ = 0;
            last_advance_seconds_ = 0;
            out.map_started = true;
            if (protocol_ == GameProtocol::classic076)
                out.wire.push_back({std::byte{31}, std::byte{0}});
        } else if (id == 19) {
            if (!receiving_ || packet.size() < 2 ||
                compressed_.size() + r.remaining() > 16U * 1024U * 1024U)
                throw std::runtime_error("Invalid classic map chunk");
            compressed_.insert(compressed_.end(), packet.begin() + 1, packet.end());
        } else if (id == 15) {
            if (!receiving_)
                throw std::runtime_error("Classic state arrived without a map");
            state_packet(packet, out);
        } else if (receiving_) {
            // Unnegotiated extension payloads have no state to replay. Do not
            // retain them during meshing/download only to ignore them later.
            if (id <= 30 && id != 28) {
                if (deferred_.size() >= 4096 ||
                    deferred_bytes_ + packet.size() > 4U * 1024U * 1024U)
                    throw std::runtime_error("Classic map bootstrap queue overflow");
                deferred_.emplace_back(packet.begin(), packet.end());
                deferred_bytes_ += packet.size();
            }
        } else if (ready_)
            game_packet(packet, out);
    } catch (const std::exception& e) {
        out.error = e.what();
        out.events.clear();
        out.wire.clear();
        out.bootstrap.reset();
    }
    return out;
}

void ClassicProtocolSession::state_packet(std::span<const std::byte> packet, ClassicIngest& out) {
    Reader r{packet};
    r.byte();
    const auto local = r.byte();
    if (local >= 128)
        throw std::runtime_error("Classic player limit exceeds supported 128 slots");
    auto boot = std::make_unique<Protocol168WorldBootstrap>();
    boot->protocol = protocol_;
    auto& state = boot->state_info;
    auto& info = boot->initial_info;
    state.player_id = local;
    state.fog_color = r.color();
    state.team1_color = r.color();
    state.team2_color = r.color();
    team_colors_ = {state.team1_color, state.team2_color};
    state.team1_name = r.string(10);
    state.team2_name = r.string(10);
    const auto mode = r.byte();
    if (mode > 1)
        throw std::runtime_error("Unknown classic game mode");
    state.gravity = 1;
    state.light_color = {220, 220, 220};
    state.ambient_light_color = {160, 160, 160};
    state.ambient_light_intensity = 0.7;
    state.light_direction = {0.5, -0.5, -1};
    state.team1_classes = {5};
    state.team2_classes = {5};
    state.team1_show_score = state.team2_show_score = true;
    state.team1_show_max_score = state.team2_show_max_score = true;
    state.team_headcount_type = 1;
    state.mode_type = mode == 0 ? 8 : 9;
    info.server_name = "Classic server";
    info.map_name = map_name_.empty() ? "Classic map" : map_name_;
    info.mode_key = state.mode_type;
    info.mode_name = mode == 0 ? "Capture the Flag" : "Territory Control";
    info.classic = true;
    info.enable_minimap = true;
    info.enable_spectator = true;
    info.enable_colour_picker = true;
    info.enable_colour_palette = true;
    info.enable_numeric_hp = true;
    info.enable_player_score = true;
    info.allow_shooting_holding_intel = true;
    info.beach_z_modifiable = false;
    info.block_wallet_multiplier = 2;
    for (std::uint8_t tool = 0; tool < 80; ++tool)
        if (tool != 4 && tool != 5 && tool != 6 && tool != 31 && tool != 37 && tool != 38)
            info.disabled_tools.push_back(tool);
    info.movement_speed_multipliers.assign(18, 1.0F);
    local_id_ = local;
    mode_ = mode;
    if (mode == 0) {
        scores_ = {r.byte(), r.byte()};
        state.team1_score = scores_[0];
        state.team2_score = scores_[1];
        state.score_limit = r.byte();
        const auto flags = r.byte();
        if (flags > 3)
            throw std::runtime_error("Invalid classic intel flags");
        for (std::uint8_t i = 0; i < 2; ++i) {
            // Bit 1: blue holds green intel; bit 2: green holds blue intel.
            if (flags & (i == 0 ? 2 : 1)) {
                carriers_[i] = r.byte();
                r.require(11);
                r.pos += 11;
            } else
                objectives_[i] = r.objective_position();
        }
        objectives_[2] = r.objective_position();
        objectives_[3] = r.objective_position();
        r.end();
    } else {
        const auto count = r.byte();
        if (count > 16)
            throw std::runtime_error("Too many classic territories");
        // piqueserver's TCState.write always reserves all 16 territory slots
        // (three floats and an owner byte each). Other implementations omit
        // the unused slots. Accept both layouts, but no partial/arbitrary tail.
        constexpr std::size_t territory_bytes = 13;
        const auto active_bytes = static_cast<std::size_t>(count) * territory_bytes;
        if (r.remaining() != active_bytes && r.remaining() != 16 * territory_bytes)
            throw std::runtime_error("Invalid classic territory state length");
        const auto padding_bytes = r.remaining() - active_bytes;
        territory_count_ = count;
        state.score_limit = count;
        for (std::uint8_t i = 0; i < count; ++i) {
            const auto position = r.vector(true);
            const auto team = r.byte();
            if (team > 2)
                throw std::runtime_error("Invalid territory team");
            objective_event(out, i, team_in(team), position);
            territories_[i].owner = team_in(team);
            if (team == 0)
                ++state.team1_score;
            if (team == 1)
                ++state.team2_score;
            event(out, TerritoryBaseStatePacket{i, 0, team_in(team), 1, 0});
            event(out, TerritoryBaseStatePacket{i, 1, team_in(team), 1, 0});
        }
        r.pos += padding_bytes;
        r.end();
    }
    auto decoded = load_classic_vxl(inflate_map(compressed_));
    if (!decoded)
        throw std::runtime_error(decoded.error);
    boot->map = std::make_shared<world::VxlMap>(std::move(*decoded.map));
    boot->map_generation = generation_;
    boot->local_player_id = local_id_;
    receiving_ = false;
    ready_ = true;
    compressed_.clear();
    for (const auto& saved : deferred_)
        game_packet(saved, out);
    deferred_.clear();
    deferred_bytes_ = 0;
    for (const auto& player : players_)
        if (player.present)
            static_cast<void>(boot->roster.apply(player.record));
    if (mode_ == 0)
        for (std::uint8_t i = 0; i < 4; ++i)
            if (i >= 2 || carriers_[i] == 255)
                objective_event(out, i, static_cast<std::uint8_t>((i % 2) + 2), objectives_[i]);
    for (const auto carrier : carriers_)
        if (carrier < players_.size())
            event(out, PickPickupPacket{carrier, 16, false});
    out.bootstrap = std::move(boot);
}

void ClassicProtocolSession::world_event(ClassicIngest& out,
                                         std::span<const std::uint8_t> ids,
                                         bool position_changed) {
    // Eleven header/trailer bytes plus 56 per native player row. WorldUpdate
    // is the hot packet path; one allocation avoids repeated vector growth.
    Writer w{2, 11U + ids.size() * 56U};
    w.integer(++world_loop_);
    w.integer<std::int16_t>(static_cast<std::int16_t>(ids.size()));
    for (auto id : ids) {
        const auto& p = players_.at(id);
        w.byte(id);
        w.vector(p.record.position);
        w.vector(p.record.orientation);
        w.vector({0, 0, 0});
        w.integer<std::int16_t>(0);
        w.integer<std::int32_t>(position_changed ? -1 : -2);
        w.integer<std::int16_t>(p.record.dead ? 0 : 100);
        w.byte(p.input);
        w.byte(static_cast<std::uint8_t>(0x10U | (p.fire & 1U) | ((p.fire & 2U) ? 0x40U : 0U)));
        w.byte(0);
        w.byte(tool_in(p.tool, p.weapon));
        w.byte(carriers_[0] == id || carriers_[1] == id ? 16 : 255);
        w.integer<std::int16_t>(0);
        w.integer<std::int16_t>(0);
        w.integer<std::int16_t>(0);
    }
    w.integer<std::int16_t>(0);
    w.integer<std::int16_t>(0);
    out.events.push_back(std::move(w.data));
}
void ClassicProtocolSession::objective_event(ClassicIngest& out,
                                             std::uint8_t id,
                                             std::uint8_t team,
                                             std::optional<std::array<float, 3>> location) {
    auto& stored = objectives_.at(id);
    const auto entity_id = static_cast<std::uint16_t>(60000U + id);
    const bool intel = mode_ == 0 && id < 2;
    if (!location) {
        if (intel && stored)
            event(out, DestroyEntityPacket{entity_id});
        if (const auto& old = objective_zones_[id]; old) {
            event(out, MinimapZoneClearPacket{old->minimum, old->maximum});
            objective_zones_[id].reset();
        }
        stored.reset();
        return;
    }
    const auto position = *location;
    stored = position;
    const auto color = team >= 2 && team <= 3 ? team_colors_[team - 2]
                                              : std::array<std::uint8_t, 3>{200, 200, 200};
    if (!intel) {
        // Match our existing CTF/TC server presentation: packet-43 zones own
        // the minimap and world markers. No extra entities or custom billboards.
        MinimapZonePacket zone;
        zone.key = 1; // Shared visibility, with the actual server team colour.
        zone.color = color;
        zone.icon_scale = 1;
        zone.icon_id = mode_ == 0 ? 6 : static_cast<std::uint8_t>(7 + std::min<unsigned>(id, 9));
        const float radius = mode_ == 0 ? 3.0F : 16.0F;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto extent = axis == 2 ? 3.0F : radius;
            zone.minimum[axis] = static_cast<std::int16_t>(
                std::clamp(position[axis] - extent, 0.0F, axis == 2 ? 239.0F : 511.0F));
            zone.maximum[axis] = static_cast<std::int16_t>(
                std::clamp(position[axis] + extent, 0.0F, axis == 2 ? 239.0F : 511.0F));
        }
        if (const auto& old = objective_zones_[id];
            old && (old->minimum != zone.minimum || old->maximum != zone.maximum))
            event(out, MinimapZoneClearPacket{old->minimum, old->maximum});
        objective_zones_[id] = ObjectiveZone{zone.minimum, zone.maximum};
        event(out, zone);
        return;
    }
    // Type 16 already provides the normal intel mesh, minimap icon and height
    // indicator. Adding a billboard here would duplicate its minimap marker.
    CreateEntityPacket entity;
    entity.entity_id = entity_id;
    entity.type = 16;
    entity.position = position;
    entity.state = team;
    entity.player_id = 255;
    entity.radius = 3;
    entity.color = {
        static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2])};
    event(out, entity);
}

void ClassicProtocolSession::game_packet(std::span<const std::byte> packet, ClassicIngest& out) {
    Reader r{packet};
    const auto kind = r.byte();
    switch (kind) {
    case 0:
    case 1: {
        if (kind == 0 && packet.size() == 4)
            return;
        auto v = r.vector(kind == 0);
        r.end();
        // Internal bridge messages 254/253 are never passed to a wire socket.
        Writer w{254};
        w.byte(kind);
        w.vector(v);
        out.events.push_back(std::move(w.data));
        break;
    }
    case 2: {
        const std::size_t stride = protocol_ == GameProtocol::classic076 ? 25 : 24;
        if (r.remaining() % stride || r.remaining() / stride > 128)
            throw std::runtime_error("Invalid classic WorldUpdate length");
        std::array<std::uint8_t, 128> changed{};
        std::size_t changed_count{};
        std::uint8_t index{};
        while (r.remaining()) {
            const auto id = protocol_ == GameProtocol::classic076 ? r.byte() : index++;
            auto pos = r.vector(true);
            auto aim = r.vector();
            auto& p = players_.at(id);
            p.record.position = pos;
            p.record.orientation = aim;
            if (p.present && id != local_id_ && !p.record.dead && p.record.team >= 2)
                changed[changed_count++] = id;
        }
        // A solo server still sends WorldUpdate. Preserve that heartbeat for
        // our existing connection watchdog even when there are no remote rows.
        world_event(out, std::span{changed.data(), changed_count}, true);
        break;
    }
    case 3:
    case 4:
    case 7: {
        const auto id = r.byte(), value = r.byte();
        r.end();
        auto& p = players_.at(id);
        if (kind == 3)
            p.input = value;
        else if (kind == 4) {
            if (value > 3)
                throw std::runtime_error("Invalid classic buttons");
            p.fire = value;
        } else {
            static_cast<void>(tool_in(value, p.weapon));
            p.tool = value;
        }
        if (p.present && id != local_id_)
            world_event(out, std::span{&id, 1});
        break;
    }
    case 5: {
        SetHpPacket hp;
        hp.health = r.byte();
        hp.damage_type = r.byte();
        hp.source = r.vector(true);
        r.end();
        event(out, hp);
        break;
    }
    case 6: {
        UseOrientedItemPacket grenade;
        grenade.player_id = r.byte();
        grenade.tool_id = 31;
        grenade.value = r.number();
        grenade.position = r.vector(true);
        grenade.velocity = r.vector();
        r.end();
        // Classic projectile velocity is in 1/32 world units, retail uses units/sec.
        for (auto& v : grenade.velocity)
            v *= 32;
        event(out, grenade);
        break;
    }
    case 8: {
        const auto id = r.byte();
        const auto c = r.color();
        r.end();
        const auto rgb = (std::uint32_t{c[0]} << 16U) | (std::uint32_t{c[1]} << 8U) | c[2];
        if (id < 128)
            players_[id].color = rgb;
        if (id >= players_.size() || !players_[id].present)
            temporary_block_color_ = rgb;
        event(out, SetColorPacket{id, rgb});
        break;
    }
    case 9:
    case 12: {
        const auto id = r.byte();
        auto candidate = players_.at(id);
        auto& p = candidate;
        p.present = true;
        p.record.player_id = id;
        p.record.class_id = 5;
        p.record.dead = false;
        std::int32_t score{};
        if (kind == 9) {
            // ExistingPlayer is roster metadata, not an authoritative local
            // spawn. Redirects may replay it before the real CreatePlayer.
            if (id == local_id_)
                p.record.dead = !joined_ || players_[id].record.dead;
            p.record.team = team_in(r.byte());
            p.weapon = r.byte();
            p.tool = r.byte();
            score = r.integer<std::int32_t>();
            const auto c = r.color();
            p.color = (std::uint32_t{c[0]} << 16U) | (std::uint32_t{c[1]} << 8U) | c[2];
        } else {
            p.weapon = r.byte();
            p.record.team = team_in(r.byte());
            p.record.position = r.vector(true);
            p.tool = 2;
            p.input = 0;
            p.fire = 0;
        }
        if (p.weapon > 2 || r.remaining() > 32)
            throw std::runtime_error("Invalid classic player");
        static_cast<void>(tool_in(p.tool, p.weapon));
        p.record.name = r.string(r.remaining());
        p.record.loadout = loadout(p.weapon);
        if (p.record.name.empty())
            p.record.name = "Player " + std::to_string(id);
        if (p.record.orientation == std::array<float, 3>{})
            p.record.orientation = {1, 0, 0};
        if (kind == 9)
            p.score = std::clamp(score, 0, 1000000);
        players_[id] = p;
        event(out, p.record);
        event(out, SetColorPacket{id, p.color});
        if (kind == 9)
            event(out, SetScorePacket{1, 0, id, score});
        if (id == local_id_ && kind == 12) {
            weapon_ = p.weapon;
            last_motion_.reset();
            last_sent_orientation_.reset();
            jump_pulse_until_ = 0;
            joined_ = true;
        } else if (id != local_id_)
            world_event(out, std::span{&id, 1});
        break;
    }
    case 10: {
        const auto id = r.byte();
        const auto team = team_in(r.byte());
        const auto weapon = r.byte();
        r.end();
        if (weapon > 2)
            throw std::runtime_error("Invalid classic weapon");
        auto& p = players_.at(id);
        p.record.team = team;
        p.weapon = weapon;
        break;
    }
    case 11: {
        const auto id = r.byte(), owner = r.byte();
        const auto position = r.objective_position();
        r.end();
        if (id >= (mode_ == 0 ? 4 : territory_count_))
            throw std::runtime_error("Invalid classic object ID");
        const auto team = mode_ == 0 ? static_cast<std::uint8_t>(id % 2 + 2) : team_in(owner);
        if (mode_ == 1 && owner > 2)
            throw std::runtime_error("Invalid territory owner");
        objective_event(out, id, team, position);
        if (mode_ == 1 && territories_[id].owner != team) {
            auto& territory = territories_[id];
            territory.owner = team;
            event(out, TerritoryBaseStatePacket{id, 5, team, territory.attacker, territory.progress});
            std::array<std::int32_t, 2> scores{};
            for (unsigned i = 0; i < territory_count_; ++i)
                if (territories_[i].owner >= 2)
                    ++scores[territories_[i].owner - 2];
            event(out, SetScorePacket{0, 0, 2, scores[0]});
            event(out, SetScorePacket{0, 0, 3, scores[1]});
        }
        break;
    }
    case 13: {
        const auto id = r.byte(), action = r.byte();
        std::array<std::int32_t, 3> cell{
            r.integer<std::int32_t>(), r.integer<std::int32_t>(), r.integer<std::int32_t>()};
        r.end();
        if (action > 3 || cell[0] < 0 || cell[0] >= 512 || cell[1] < 0 || cell[1] >= 512 ||
            cell[2] < 0 || cell[2] >= 64)
            throw std::runtime_error("Invalid classic block action");
        if (action == 0) {
            const auto color = id < players_.size() && players_[id].present
                                   ? players_[id].color : temporary_block_color_;
            event(out,
                  BlockBuildColoredPacket{world_loop_,
                                          id,
                                          static_cast<std::int16_t>(cell[0]),
                                          static_cast<std::int16_t>(cell[1]),
                                          static_cast<std::int16_t>(cell[2] + 176),
                                          color});
        } else {
            // Internal exact removal events avoid retail blast/damage semantics.
            Writer w{252};
            w.byte(action);
            w.byte(id);
            for (auto c : cell)
                w.integer(c);
            out.events.push_back(std::move(w.data));
        }
        break;
    }
    case 14: {
        BlockLinePacket line;
        line.player_id = r.byte();
        line.loop_count = world_loop_;
        for (auto* endpoint : {&line.start, &line.end})
            for (std::size_t i = 0; i < 3; ++i) {
                const auto c = r.integer<std::int32_t>();
                // The server can edit the protected water/bedrock layers.
                // Only client requests are restricted to z < 62.
                if (c < 0 || c >= (i == 2 ? 64 : 512))
                    throw std::runtime_error("Invalid classic block line");
                (*endpoint)[i] = static_cast<std::int16_t>(c + (i == 2 ? 176 : 0));
            }
        r.end();
        // Original servers do not echo the builder's SetColor back to them.
        // Seed the shared terrain palette for a line from the same colour
        // cache used for individual BlockAction builds.
        const auto color = line.player_id < players_.size() && players_[line.player_id].present
                               ? players_[line.player_id].color : temporary_block_color_;
        event(out, SetColorPacket{line.player_id, color});
        event(out, line);
        break;
    }
    case 16: {
        KillActionPacket kill;
        kill.player_id = r.byte();
        kill.killer_id = r.byte();
        const auto type = r.byte();
        constexpr std::array<std::uint8_t, 7> kills{0, 1, 2, 22, 7, 8, 10};
        if (type >= kills.size())
            throw std::runtime_error("Invalid classic kill type");
        kill.kill_type = kills[type];
        kill.respawn_time = r.byte();
        r.end();
        if (type >= 4)
            kill.killer_id = kill.player_id;
        players_.at(kill.player_id).record.dead = true;
        event(out, kill);
        if (kill.killer_id != kill.player_id) {
            auto& scorer = players_.at(kill.killer_id);
            scorer.score = std::min(1000000, scorer.score + 1);
            event(out, SetScorePacket{1, 0, kill.killer_id, scorer.score});
        }
        break;
    }
    case 17: {
        ChatMessagePacket chat;
        chat.player_id = r.byte();
        chat.chat_type = r.byte();
        if (r.remaining() > 512)
            throw std::runtime_error("Classic chat too long");
        chat.value = r.string(r.remaining());
        if (chat.chat_type > 2)
            chat.chat_type = 2;
        event(out, chat);
        break;
    }
    case 20: {
        const auto id = r.byte();
        r.end();
        players_.at(id) = {};
        event(out, PlayerLeftPacket{id});
        break;
    }
    case 21: {
        const auto territory = r.byte();
        r.byte();
        const auto team = team_in(r.byte());
        r.end();
        if (mode_ != 1 || territory >= territory_count_)
            throw std::runtime_error("Invalid captured territory");
        auto& state = territories_[territory];
        state.owner = team;
        state.attacker = 1;
        state.progress = state.rate = 0;
        objective_event(out, territory, team, objectives_.at(territory));
        event(out, TerritoryBaseStatePacket{territory, 5, team, 1, 0});
        std::array<std::int32_t, 2> scores{};
        for (unsigned i = 0; i < territory_count_; ++i)
            if (territories_[i].owner >= 2)
                ++scores[territories_[i].owner - 2];
        event(out, SetScorePacket{0, 0, 2, scores[0]});
        event(out, SetScorePacket{0, 0, 3, scores[1]});
        break;
    }
    case 22: {
        const auto territory = r.byte(), team = team_in(r.byte());
        const auto rate = r.integer<std::int8_t>();
        const auto progress = r.number();
        r.end();
        if (mode_ != 1 || territory >= territory_count_ || team < 2 || progress < -0.1F ||
            progress > 1.1F)
            throw std::runtime_error("Invalid territory progress");
        auto& state = territories_[territory];
        state.attacker = team;
        state.progress = std::clamp(progress, 0.0F, 1.0F);
        state.rate = rate * 0.05F;
        event(out, TerritoryBaseStatePacket{territory, 5, state.owner, team, state.progress});
        break;
    }
    case 23: {
        const auto id = r.byte();
        const auto winning = r.byte();
        r.end();
        auto& scorer = players_.at(id);
        const auto team = scorer.record.team;
        if (team >= 2) {
            scores_[team - 2] = std::min(1000000, scores_[team - 2] + 1);
            carriers_[3 - team] = 255;
            event(out, SetScorePacket{0, 0, team, scores_[team - 2]});
            scorer.score = std::min(1000000, scorer.score + 10);
            event(out, SetScorePacket{1, 0, id, scorer.score});
        }
        event(out, DropPickupPacket{world_loop_, id, 16, players_[id].record.position, {}});
        if (winning)
            event(out,
                  ChatMessagePacket{255,
                                    2,
                                    team == 2 ? "Blue team captured the final intel."
                                              : "Green team captured the final intel."});
        break;
    }
    case 24: {
        const auto id = r.byte();
        r.end();
        const auto team = players_.at(id).record.team;
        if (team >= 2) {
            const auto flag = static_cast<std::uint8_t>(3 - team);
            carriers_[flag] = id;
            event(out, DestroyEntityPacket{static_cast<std::uint16_t>(60000U + flag)});
        }
        event(out, PickPickupPacket{id, 16, false});
        break;
    }
    case 25: {
        const auto id = r.byte();
        const auto position = r.objective_position();
        r.end();
        const auto team = players_.at(id).record.team;
        if (team >= 2) {
            const auto flag = static_cast<std::uint8_t>(3 - team);
            carriers_[flag] = 255;
            objective_event(out, flag, static_cast<std::uint8_t>(flag + 2), position);
        }
        // This shared packet detaches the carried visual; objective_event owns
        // the ground entity. Never forward a hidden position to the renderer.
        event(out,
              DropPickupPacket{
                  world_loop_, id, 16, position.value_or(players_[id].record.position), {}});
        break;
    }
    case 26: {
        const auto id = r.byte();
        r.end();
        event(out, RestockPacket{id, 0});
        event(out, RestockPacket{id, 5});
        if (id == local_id_)
            event(out, SetHpPacket{100, 0, {}});
        break;
    }
    case 27: {
        r.byte();
        const auto color = r.color();
        r.end();
        event(out, FogColorPacket{color});
        break;
    }
    case 28: {
        const auto id = r.byte(), clip = r.byte(), reserve = r.byte();
        r.end();
        if (id == local_id_) {
            Writer w{253};
            w.byte(clip);
            w.byte(reserve);
            out.events.push_back(std::move(w.data));
        } else
            event(out, WeaponReloadPacket{id, classic_weapon_tool(players_.at(id).weapon), false});
        break;
    }
    case 29:
    case 30: {
        // These are client requests, not authoritative state. pyspades forks
        // broadcast uninitialized ChangeWeapon fields before Kill/CreatePlayer.
        // ZeroSpades ignores both notifications; applying them can change the
        // wrong player's gun/team or invent a local spawn.
        r.byte();
        r.byte();
        r.end();
        break;
    }
    default:
        break; // No extension was advertised, so do not interpret its payload.
    }
}

ClassicPackets ClassicProtocolSession::advance(double seconds) {
    ClassicIngest out;
    if (!ready_ || mode_ != 1 || !std::isfinite(seconds))
        return {};
    if (last_advance_seconds_ == 0) {
        last_advance_seconds_ = seconds;
        return {};
    }
    const auto dt = seconds - last_advance_seconds_;
    if (dt < 0.1)
        return {};
    last_advance_seconds_ = seconds;
    for (std::uint8_t i = 0; i < territory_count_; ++i) {
        auto& state = territories_[i];
        const auto progress =
            std::clamp(state.progress + static_cast<float>(dt) * state.rate, 0.0F, 1.0F);
        if (progress != state.progress) {
            state.progress = progress;
            event(out, TerritoryBaseStatePacket{i, 5, state.owner, state.attacker, progress});
        }
        // This only selects our existing capture-panel presentation. Ownership
        // and capture completion still wait for the original server's packet.
        bool inside = last_motion_ && last_motion_->alive && objectives_[i];
        if (inside)
            for (std::size_t axis = 0; axis < 3; ++axis)
                inside =
                    inside && std::abs(last_motion_->position[axis] - (*objectives_[i])[axis]) < 16;
        if (inside != state.local_inside) {
            state.local_inside = inside;
            event(out,
                  TerritoryBaseStatePacket{i,
                                           static_cast<std::uint8_t>(inside ? 3 : 4),
                                           state.owner,
                                           state.attacker,
                                           progress});
        }
    }
    return std::move(out.events);
}

ClassicPackets ClassicProtocolSession::translate_client(std::span<const std::byte> packet) {
    ClassicPackets result;
    if (!ready_ || packet.empty())
        return result;
    try {
        Reader r{packet};
        const auto id = r.byte();
        // Until CreatePlayer confirms a life, only the initial join and the
        // locally remembered weapon choice belong to this connection phase.
        if (!joined_ && id != 15 && id != 13)
            return result;
        if (id == 15) {
            const auto team = team_out(r.byte());
            r.byte();
            r.byte();
            r.byte();
            auto name = r.string(r.remaining(), false);
            Writer w{9};
            w.byte(local_id_);
            w.byte(team);
            w.byte(weapon_);
            w.byte(2);
            w.integer<std::int32_t>(0);
            w.byte(112);
            w.byte(112);
            w.byte(112);
            w.text(classic_encode_text(name.empty() ? name_ : name, 15), 15);
            result.push_back(std::move(w.data));
        } else if (id == 77) {
            Writer w{29};
            r.byte(); // The server-assigned local ID owns every client request.
            w.byte(local_id_);
            w.byte(team_out(r.byte()));
            r.end();
            result.push_back(std::move(w.data));
        } else if (id == 13) {
            auto decoded = decode_weapon_packet(packet);
            if (!decoded)
                return {};
            if (const auto* p = std::get_if<SetClassLoadoutPacket>(&*decoded.packet))
                for (auto tool : p->loadout)
                    if (auto weapon = classic_weapon_id(tool)) {
                        if (*weapon != weapon_) {
                            weapon_ = *weapon;
                            if (!joined_)
                                break;
                            Writer w{30};
                            w.byte(local_id_);
                            w.byte(weapon_);
                            result.push_back(std::move(w.data));
                        }
                        break;
                    }
        } else if (id == 49) {
            auto decoded = decode_runtime_packet(packet);
            if (!decoded)
                return {};
            if (const auto* p = std::get_if<ChatMessagePacket>(&*decoded.packet)) {
                Writer w{17};
                w.byte(local_id_);
                w.byte(p->chat_type == 1 ? 1 : 0);
                w.text(classic_encode_text(p->value, 90));
                result.push_back(std::move(w.data));
            }
        } else if (id == 11) {
            auto decoded = decode_terrain_packet(packet);
            if (!decoded)
                return {};
            if (const auto* p = std::get_if<SetColorPacket>(&*decoded.packet)) {
                players_[local_id_].color = p->color & 0xFFFFFFU;
                Writer w{8};
                w.byte(local_id_);
                w.byte(static_cast<std::uint8_t>(p->color));
                w.byte(static_cast<std::uint8_t>(p->color >> 8));
                w.byte(static_cast<std::uint8_t>(p->color >> 16));
                result.push_back(std::move(w.data));
            }
        } else if (id == 76) {
            auto decoded = decode_weapon_packet(packet);
            if (!decoded)
                return {};
            if (const auto* p = std::get_if<WeaponReloadPacket>(&*decoded.packet);
                p && !p->is_done) {
                result.push_back(
                    {std::byte{28}, static_cast<std::byte>(local_id_), std::byte{0}, std::byte{0}});
            }
        }
        // All other retail IDs are deliberately not sent. Position, hit,
        // grenade and terrain requests have separate classic encoders.
    } catch (const std::exception&) {
        return {};
    }
    return result;
}

bool ClassicProtocolSession::accepts_client_action(std::span<const std::byte> packet) const noexcept {
    if (!ready_ || !joined_ || players_[local_id_].record.dead ||
        players_[local_id_].record.team < 2 || !valid_classic_client_action(packet))
        return false;
    // HitPacket's ID identifies the victim; grenade and terrain IDs identify
    // the sender. Old-life requests must not leak through a redirected slot.
    return packet[0] == std::byte{5} || packet[1] == static_cast<std::byte>(local_id_);
}

ClassicPackets ClassicProtocolSession::motion(const ClassicMotion& requested, double seconds) {
    auto value = requested;
    ClassicPackets packets;
    if (!ready_ || !joined_ || !std::isfinite(seconds))
        return packets;
    if (players_[local_id_].record.dead || players_[local_id_].record.team < 2)
        value.alive = false;
    if (!value.alive) {
        if (last_motion_ && last_motion_->alive) {
            packets.push_back({std::byte{3}, static_cast<std::byte>(local_id_), std::byte{0}});
            packets.push_back({std::byte{4}, static_cast<std::byte>(local_id_), std::byte{0}});
        }
        last_motion_.reset();
        jump_pulse_until_ = 0;
        return packets;
    }
    for (auto f : value.position)
        if (!std::isfinite(f) || std::abs(f) > 4096)
            return packets;
    for (auto f : value.orientation)
        if (!std::isfinite(f) || std::abs(f) > 4096)
            return packets;
    // The network thread can sample several fixed ticks between ENet flushes.
    // Keep one accepted jump visible across a server tick, not two contradictory
    // InputData packets in the same flush. This does not create extra jumps.
    if ((value.movement & 16U) && (!last_motion_ || !(last_motion_->movement & 16U)))
        jump_pulse_until_ = seconds + 1.0 / 30.0;
    if (seconds < jump_pulse_until_)
        value.movement |= 16U;
    const auto tool = tool_out(value.tool);
    if (!tool)
        return packets;
    if (!last_motion_ || last_motion_->tool != value.tool) {
        Writer w{7};
        w.byte(local_id_);
        w.byte(*tool);
        packets.push_back(std::move(w.data));
    }
    if (!last_motion_ || last_motion_->movement != value.movement) {
        Writer w{3};
        w.byte(local_id_);
        w.byte(value.movement);
        packets.push_back(std::move(w.data));
    }
    const auto buttons =
        static_cast<std::uint8_t>((value.actions & 1U) | ((value.actions & 6U) ? 2U : 0U));
    const auto old = last_motion_
                         ? static_cast<std::uint8_t>((last_motion_->actions & 1U) |
                                                     ((last_motion_->actions & 6U) ? 2U : 0U))
                         : 255;
    if (buttons != old) {
        Writer w{4};
        w.byte(local_id_);
        w.byte(buttons);
        packets.push_back(std::move(w.data));
    }
    if ((!last_sent_orientation_ || *last_sent_orientation_ != value.orientation) &&
        seconds - last_orientation_seconds_ >= 1.0 / 120.0) {
        Writer w{1};
        w.vector(value.orientation);
        packets.push_back(std::move(w.data));
        last_orientation_seconds_ = seconds;
        last_sent_orientation_ = value.orientation;
    }
    if (seconds - last_position_seconds_ >= 1.0) {
        Writer w{0};
        w.vector(value.position, true);
        packets.push_back(std::move(w.data));
        last_position_seconds_ = seconds;
    }
    last_motion_ = value;
    return packets;
}
} // namespace battlespades::network
