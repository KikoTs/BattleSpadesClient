#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/world/classic_movement.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/jetpack_death.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <utility>

namespace battlespades::network {
namespace {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_{bytes} {}
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] bool done() const noexcept { return remaining() == 0U; }
    [[nodiscard]] std::optional<std::uint8_t> u8() noexcept {
        if (remaining() < 1U) return std::nullopt;
        return std::to_integer<std::uint8_t>(bytes_[offset_++]);
    }
    template <typename Integer>
    [[nodiscard]] std::optional<Integer> integer() noexcept {
        static_assert(std::is_integral_v<Integer>);
        if (remaining() < sizeof(Integer)) return std::nullopt;
        using Unsigned = std::make_unsigned_t<Integer>;
        Unsigned value{};
        for (std::size_t index{}; index < sizeof(Integer); ++index) {
            value = static_cast<Unsigned>(
                value | (static_cast<Unsigned>(std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                         << (index * 8U)));
        }
        offset_ += sizeof(Integer);
        return static_cast<Integer>(value);
    }
    [[nodiscard]] std::optional<std::string> string(std::size_t maximum) {
        const auto begin = offset_;
        while (offset_ < bytes_.size() && bytes_[offset_] != std::byte{0U}) {
            if (offset_ - begin >= maximum) return std::nullopt;
            ++offset_;
        }
        if (offset_ == bytes_.size()) return std::nullopt;
        std::string result;
        result.reserve(offset_ - begin);
        for (auto index = begin; index < offset_; ++index) {
            result.push_back(static_cast<char>(
                std::to_integer<std::uint8_t>(bytes_[index])));
        }
        ++offset_;
        return result;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

class Writer final {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    template <typename Integer>
    void integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        const auto raw = static_cast<Unsigned>(value);
        for (std::size_t index{}; index < sizeof(Integer); ++index) {
            u8(static_cast<std::uint8_t>(raw >> (index * 8U)));
        }
    }
    void string(std::string_view value) {
        for (const char character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
        u8(0U);
    }
    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

template <typename Value>
[[nodiscard]] bool required(std::optional<Value> value, Value& output) {
    if (!value.has_value()) return false;
    output = std::move(*value);
    return true;
}

[[nodiscard]] float from_fixed(std::int16_t raw) noexcept {
    const auto bits = static_cast<std::uint16_t>(raw);
    const float magnitude = static_cast<float>(bits & 0x7FFFU) / 64.0F;
    return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
}

[[nodiscard]] std::int16_t to_fixed(float value) noexcept {
    if (!std::isfinite(value)) return 0;
    const auto magnitude = static_cast<std::uint16_t>(std::min(
        std::lround(std::abs(value) * 64.0F), 0x7FFFL));
    return static_cast<std::int16_t>(
        magnitude | (value < 0.0F ? 0x8000U : 0U));
}

[[nodiscard]] bool finite_vector(const std::array<float, 3U>& value) noexcept {
    return std::ranges::all_of(value, [](float element) {
        return std::isfinite(element);
    });
}

[[nodiscard]] bool valid_packet(const CreatePlayerPacket& packet,
                                std::string& error) {
    if (packet.player_id >= 128U) {
        error = "CreatePlayer player id uses the palette/reserved high bit";
        return false;
    }
    if (world::find_class_definition(packet.class_id) == nullptr) {
        error = "CreatePlayer class id is outside the retail catalog";
        return false;
    }
    if (packet.team > 3U) {
        error = "CreatePlayer team is outside spectator/neutral/Blue/Green";
        return false;
    }
    if (packet.name.empty() || packet.name.size() > 31U ||
        !finite_vector(packet.position) || !finite_vector(packet.orientation)) {
        error = "CreatePlayer has an invalid name or non-finite transform";
        return false;
    }
    const double length_squared =
        static_cast<double>(packet.orientation[0U]) * packet.orientation[0U] +
        static_cast<double>(packet.orientation[1U]) * packet.orientation[1U] +
        static_cast<double>(packet.orientation[2U]) * packet.orientation[2U];
    if (length_squared < 0.25 || length_squared > 2.25) {
        error = "CreatePlayer orientation is degenerate or non-unit";
        return false;
    }
    if (packet.loadout.size() > 65U || packet.prefabs.size() > 16U) {
        error = "CreatePlayer loadout or prefab collection exceeds its bound";
        return false;
    }
    // CreatePlayer.loadout is an equipment list, not only the 0..64 weapon
    // catalog. Retail appends jetpack/glider inventory ids (for example 68)
    // to the same byte array. Preserve unknown equipment for class replication;
    // selection/render code still gates concrete weapon ids independently.
    if (std::ranges::any_of(packet.prefabs, [](const std::string& prefab) {
            return prefab.size() > 127U;
        })) {
        // Empty prefab slots are legal in retail class selections and must be
        // preserved as positional carousel entries.
        error = "CreatePlayer contains an oversized prefab name";
        return false;
    }
    return true;
}

} // namespace

CreatePlayerDecodeResult decode_create_player(std::span<const std::byte> payload) {
    Reader reader{payload};
    std::uint8_t id{};
    CreatePlayerPacket packet;
    std::uint8_t demo{};
    std::uint8_t dead{};
    std::array<std::int16_t, 6U> transform{};
    if (!required(reader.u8(), id) || id != CreatePlayerPacket::id ||
        !required(reader.u8(), packet.player_id) || !required(reader.u8(), demo) ||
        !required(reader.u8(), packet.class_id) || !required(reader.u8(), packet.team) ||
        !required(reader.u8(), dead) || !required(reader.u8(), packet.local_language)) {
        return {std::nullopt, "malformed CreatePlayer(28) header"};
    }
    for (auto& value : transform) {
        if (!required(reader.integer<std::int16_t>(), value)) {
            return {std::nullopt, "malformed CreatePlayer(28) transform"};
        }
    }
    for (std::size_t axis{}; axis < 3U; ++axis) {
        packet.position[axis] = from_fixed(transform[axis]);
        packet.orientation[axis] = from_fixed(transform[axis + 3U]);
    }
    if (!required(reader.string(31U), packet.name)) {
        return {std::nullopt, "malformed CreatePlayer(28) name"};
    }
    std::uint8_t count{};
    if (!required(reader.u8(), count) || count > 65U) {
        return {std::nullopt, "malformed CreatePlayer(28) loadout count"};
    }
    packet.loadout.reserve(count);
    for (std::uint16_t index{}; index < count; ++index) {
        std::uint8_t tool{};
        if (!required(reader.u8(), tool)) {
            return {std::nullopt, "malformed CreatePlayer(28) loadout"};
        }
        packet.loadout.push_back(tool);
    }
    if (!required(reader.u8(), count) || count > 16U) {
        return {std::nullopt, "malformed CreatePlayer(28) prefab count"};
    }
    packet.prefabs.reserve(count);
    for (std::uint16_t index{}; index < count; ++index) {
        std::string prefab;
        if (!required(reader.string(127U), prefab)) {
            return {std::nullopt, "malformed CreatePlayer(28) prefab"};
        }
        packet.prefabs.push_back(std::move(prefab));
    }
    if (!reader.done()) {
        return {std::nullopt, "CreatePlayer(28) has trailing bytes"};
    }
    packet.demo_player = demo != 0U;
    packet.dead = dead != 0U;
    std::string error;
    if (!valid_packet(packet, error)) return {std::nullopt, std::move(error)};
    return {std::move(packet), {}};
}

std::vector<std::byte> encode_packet(const CreatePlayerPacket& packet) {
    std::string error;
    if (!valid_packet(packet, error)) return {};
    Writer writer;
    writer.u8(CreatePlayerPacket::id);
    writer.u8(packet.player_id);
    writer.u8(packet.demo_player ? 1U : 0U);
    writer.u8(packet.class_id);
    writer.u8(packet.team);
    writer.u8(packet.dead ? 1U : 0U);
    writer.u8(packet.local_language);
    for (const float value : packet.position) writer.integer(to_fixed(value));
    for (const float value : packet.orientation) writer.integer(to_fixed(value));
    writer.string(packet.name);
    writer.u8(static_cast<std::uint8_t>(packet.loadout.size()));
    for (const auto tool : packet.loadout) writer.u8(tool);
    writer.u8(static_cast<std::uint8_t>(packet.prefabs.size()));
    for (const auto& prefab : packet.prefabs) writer.string(prefab);
    return std::move(writer).take();
}

bool Protocol168Roster::apply(const CreatePlayerPacket& packet,
                              std::string* error) {
    std::string detail;
    if (!valid_packet(packet, detail)) {
        if (error != nullptr) *error = std::move(detail);
        return false;
    }
    const auto id = packet.player_id;
    std::vector<std::uint8_t> retained_ugc_tools;
    bool retained_dominating_local_player{};
    bool retained_dominated_by_local_player{};
    std::uint32_t retained_running_local_player_kills{};
    // CreatePlayer has no UGC suffix. A respawn replaces the life generation
    // but not the already-acknowledged selection; preserve it only for the
    // same named player/team so an unexpected id reuse cannot inherit tools.
    if (players_[id].has_value() && players_[id]->name == packet.name &&
        players_[id]->team == packet.team) {
        retained_ugc_tools = players_[id]->ugc_tools;
        // Neither relationship bit is carried by CreatePlayer. The retail
        // GameScene stores them on the persistent player object, so an
        // ordinary respawn must not silently erase the scoreboard markers.
        retained_dominating_local_player =
            players_[id]->dominating_local_player;
        retained_dominated_by_local_player =
            players_[id]->dominated_by_local_player;
        retained_running_local_player_kills =
            players_[id]->running_local_player_kills;
    }
    auto& generation = generations_[id];
    ++generation;
    if (generation == 0U) ++generation;
    RemotePlayerReplica replica;
    replica.player_id = id;
    replica.generation = generation;
    replica.class_id = packet.class_id;
    replica.team = packet.team;
    replica.dead = packet.dead;
    replica.demo_player = packet.demo_player;
    replica.local_language = packet.local_language;
    replica.position = {packet.position[0U], packet.position[1U],
                        packet.position[2U]};
    replica.orientation = {packet.orientation[0U], packet.orientation[1U],
                           packet.orientation[2U]};
    replica.name = packet.name;
    replica.loadout = packet.loadout;
    replica.prefabs = packet.prefabs;
    replica.ugc_tools = std::move(retained_ugc_tools);
    replica.dominating_local_player = retained_dominating_local_player;
    replica.dominated_by_local_player = retained_dominated_by_local_player;
    replica.running_local_player_kills = retained_running_local_player_kills;
    if (!replica.loadout.empty()) replica.tool_id = replica.loadout.front();
    players_[id] = std::move(replica);
    if (error != nullptr) error->clear();
    return true;
}

bool Protocol168Roster::apply(std::span<const std::byte> payload,
                              std::string* error) {
    const auto decoded = decode_create_player(payload);
    if (!decoded) {
        if (error != nullptr) *error = decoded.error;
        return false;
    }
    return apply(*decoded.packet, error);
}

void Protocol168Roster::remove(std::uint8_t player_id) noexcept {
    if (player_id < players_.size()) players_[player_id].reset();
}

bool Protocol168Roster::update_transform(std::uint8_t player_id,
                                         world::Vec3 position,
                                         world::Vec3 orientation) noexcept {
    if (player_id >= players_.size() || !players_[player_id].has_value()) {
        return false;
    }
    players_[player_id]->position = position;
    players_[player_id]->orientation = orientation;
    return true;
}

bool Protocol168Roster::update_world_state(
    const WorldPlayerWeaponRow& row, std::optional<std::int32_t> world_loop,
    bool local_owner) noexcept {
    if (row.player_id >= players_.size() || !players_[row.player_id].has_value()) {
        return false;
    }
    auto& player = *players_[row.player_id];
    // A new life always arrives as CreatePlayer (the server withholds its
    // rows until that is acknowledged). A live row that lands after KillAction
    // is stale and used to stand the corpse back up and move it.
    if (player.dead && row.health > 0) return false;
    if (world_loop.has_value()) {
        if (*world_loop < 0) return false;
        if (player.world_update_loop.has_value()) {
            // Split owner packets carry the last observer loop, which may lag
            // behind newer observer packets. Their ACK orders reconciliation.
            if (local_owner) {
                if (row.acknowledged_client_loop < player.acknowledged_client_loop ||
                    (row.acknowledged_client_loop == player.acknowledged_client_loop &&
                     *world_loop <= *player.world_update_loop)) return false;
            } else if (*world_loop <= *player.world_update_loop) {
                return false;
            }
        }
        player.world_update_loop = world_loop;
    }
    player.position = {row.position[0U], row.position[1U], row.position[2U]};
    player.orientation = {row.orientation[0U], row.orientation[1U],
                          row.orientation[2U]};
    player.velocity = {row.velocity[0U], row.velocity[1U], row.velocity[2U]};
    player.health = row.health;
    player.ping = row.ping;
    player.dead = row.health <= 0;
    player.acknowledged_client_loop = row.acknowledged_client_loop;
    player.input_flags = row.input_flags;
    player.action_flags = row.action_flags;
    player.state_flags = row.state_flags;
    // Retail applies the row, then rejects a tool id outside its selectable
    // range (0..64) and keeps the held tool. The server writes 0xFF there for
    // owner rows and for a body with no tool; adopting it made observers drop
    // the held tool art (and log it) instead of keeping the last one.
    constexpr std::uint8_t retail_tool_count{65U};
    if (row.tool_id < retail_tool_count) player.tool_id = row.tool_id;
    player.pickup_id = row.pickup_id;
    player.jetpack_fuel = row.jetpack_fuel;
    player.spawn_protection = row.spawn_protection;
    player.weapon_deployment_yaw = row.weapon_deployment_yaw;
    return true;
}

bool Protocol168Roster::update_health(std::uint8_t player_id,
                                      std::int16_t health) noexcept {
    if (player_id >= players_.size() || !players_[player_id].has_value()) {
        return false;
    }
    auto& player = *players_[player_id];
    player.health = health;
    player.dead = health <= 0;
    return true;
}

bool Protocol168Roster::update_jetpack_fuel(std::uint8_t player_id,
                                            float fuel) noexcept {
    if (player_id >= players_.size() || !players_[player_id].has_value() ||
        !std::isfinite(fuel)) {
        return false;
    }
    players_[player_id]->jetpack_fuel = fuel;
    return true;
}

bool Protocol168Roster::update_mode_visibility(
    std::uint16_t player_id,
    std::optional<bool> high_minimap_visibility,
    std::optional<bool> chase_cam) noexcept {
    if (player_id >= players_.size() ||
        !players_[player_id].has_value()) {
        return false;
    }
    auto& player = *players_[player_id];
    if (high_minimap_visibility.has_value()) {
        player.high_minimap_visibility = *high_minimap_visibility;
    }
    if (chase_cam.has_value()) {
        player.chase_cam = *chase_cam;
    }
    return true;
}

bool Protocol168Roster::update_pickup(std::uint8_t player_id,
                                      std::uint8_t pickup_id) noexcept {
    if (player_id >= players_.size() || !players_[player_id].has_value()) {
        return false;
    }
    players_[player_id]->pickup_id = pickup_id;
    return true;
}

bool Protocol168Roster::apply_kill_relationships(
    std::uint8_t victim_id, std::uint8_t killer_id,
    std::uint8_t local_player_id, bool domination, bool revenge,
    bool team_change_kill) noexcept {
    auto mutable_player = [this](std::uint8_t player_id)
        -> RemotePlayerReplica* {
        return player_id < players_.size() && players_[player_id].has_value()
                   ? &*players_[player_id]
                   : nullptr;
    };

    auto* victim = mutable_player(victim_id);
    auto* killer = mutable_player(killer_id);
    bool changed = false;

    // gameScene.process_packet_kill_action (0x10194940, lines 3672-3676):
    // only a forced/team-change kill resets relationship markers, on both the
    // killer and the victim. An ordinary death keeps them: a player who is
    // dominating you stays marked until revenge (BS/docs/KILLFEED_RETAIL.md).
    if (team_change_kill) {
        for (auto* player : {killer, victim}) {
            if (player != nullptr) {
                player->dominating_local_player = false;
                player->dominated_by_local_player = false;
                changed = true;
            }
        }
        return changed;
    }

    // Line 3688: the local-kill branch requires killer != victim; line 3708
    // is the `elif` for a local death.
    if (killer_id == local_player_id && killer_id != victim_id) {
        if (victim != nullptr) {
            if (revenge) victim->dominating_local_player = false;
            if (domination) victim->dominated_by_local_player = true;
            victim->running_local_player_kills = 0U;
            changed = true;
        }
    } else if (victim_id == local_player_id && killer != nullptr) {
        if (revenge) killer->dominated_by_local_player = false;
        if (domination) killer->dominating_local_player = true;
        if (killer_id != victim_id) ++killer->running_local_player_kills;
        changed = true;
    }
    return changed;
}

bool Protocol168Roster::update_loadout(
    std::uint8_t player_id, std::uint8_t class_id,
    std::span<const std::uint8_t> loadout,
    std::span<const std::string> prefabs,
    std::span<const std::uint8_t> ugc_tools) noexcept {
    if (player_id >= players_.size() || !players_[player_id].has_value()) {
        return false;
    }
    auto& player = *players_[player_id];
    player.class_id = class_id;
    player.loadout.assign(loadout.begin(), loadout.end());
    player.prefabs.assign(prefabs.begin(), prefabs.end());
    player.ugc_tools.assign(ugc_tools.begin(), ugc_tools.end());
    return true;
}

void Protocol168Roster::clear() noexcept {
    for (auto& player : players_) player.reset();
}

const RemotePlayerReplica*
Protocol168Roster::player(std::uint8_t player_id) const noexcept {
    return player_id < players_.size() && players_[player_id].has_value()
               ? &*players_[player_id]
               : nullptr;
}

std::vector<RemotePlayerReplica> Protocol168Roster::players() const {
    std::vector<RemotePlayerReplica> result;
    for (const auto& player : players_) {
        if (player.has_value()) result.push_back(*player);
    }
    return result;
}

RemoteMotionSample remote_motion_sample(const RemotePlayerReplica& replica,
                                        double movement_speed_scale) noexcept {
    RemoteMotionSample sample;
    sample.position = replica.position;
    sample.orientation = replica.orientation;
    sample.velocity = replica.velocity;
    sample.input_flags = replica.input_flags;
    sample.hover = (replica.action_flags & 0x80U) != 0U;
    // Protocol ids 66..69 map to world.pyd's compact 1..4 pack enum.
    if (const auto pack = world::retail_jetpack_id(replica.loadout, replica.ugc_tools);
        pack.has_value() && *pack >= 66U && *pack <= 69U) {
        sample.jetpack = static_cast<std::uint8_t>(*pack - 65U);
    }
    sample.jetpack_active = (replica.action_flags & 0x04U) != 0U;
    sample.parachute = std::ranges::find(replica.loadout, std::uint8_t{72U}) !=
                       replica.loadout.end();
    sample.parachute_active = (replica.state_flags & 0x01U) != 0U;
    sample.dead = replica.dead;
    sample.class_id = replica.class_id;
    sample.movement_speed_scale =
        std::isfinite(movement_speed_scale) && movement_speed_scale > 0.0 ? movement_speed_scale
                                                                          : 1.0;
    return sample;
}

namespace {

[[nodiscard]] world::Vec3 unit_orientation(world::Vec3 value) noexcept {
    const auto length = std::hypot(value.x, value.y, value.z);
    if (!(length > 1.0e-9)) return {1.0, 0.0, 0.0};
    return {value.x / length, value.y / length, value.z / length};
}

} // namespace

void RemoteMotionInterpolator::reset(RemoteMotionSample sample) noexcept {
    sample.orientation = unit_orientation(sample.orientation);
    current_ = sample;
    body_ = {};
    body_.position = sample.position;
    body_.velocity = sample.velocity;
    body_.orientation = sample.orientation;
    body_.crouch = (sample.input_flags & 0x20U) != 0U;
    body_.jetpack = sample.jetpack;
    body_.jetpack_active = sample.jetpack_active;
    body_.jetpack_passive = sample.jetpack == 2U && sample.jetpack_active;
    body_.parachute = sample.parachute;
    body_.parachute_active = sample.parachute_active;
    body_.burdened = sample.burdened;
    initialized_ = true;
}

void RemoteMotionInterpolator::push(RemoteMotionSample sample,
                                    double snapshot_interval) noexcept {
    static_cast<void>(snapshot_interval);
    const auto finite = [](const world::Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    if (!finite(sample.position) || !finite(sample.orientation) ||
        !finite(sample.velocity)) {
        return;
    }
    if (!initialized_) {
        reset(sample);
        return;
    }
    // Retail snaps the world object to the newest network position/velocity.
    // Its airborne, wade, climb and fall bookkeeping belong to the local
    // simulation of that object and survive the snap.
    const auto airborne = body_.airborne;
    const auto wade = body_.wade;
    const auto fall_distance = body_.fall_distance;
    const auto climb_timer = body_.climb_timer;
    const auto climb_slowdown = body_.climb_slowdown;
    reset(sample);
    body_.airborne = airborne;
    body_.wade = wade;
    body_.fall_distance = fall_distance;
    body_.climb_timer = climb_timer;
    body_.climb_slowdown = climb_slowdown;
}

void RemoteMotionInterpolator::tick(double dt, const world::VxlMap* map,
                                    double world_gravity, bool classic) noexcept {
    if (!initialized_ || !std::isfinite(dt) || dt <= 0.0 || current_.dead) return;
    world::PlayerInputState input;
    input.forward = (current_.input_flags & 0x01U) != 0U;
    input.backward = (current_.input_flags & 0x02U) != 0U;
    input.left = (current_.input_flags & 0x04U) != 0U;
    input.right = (current_.input_flags & 0x08U) != 0U;
    input.jump = (current_.input_flags & 0x10U) != 0U;
    input.crouch = (current_.input_flags & 0x20U) != 0U;
    input.sneak = (current_.input_flags & 0x40U) != 0U;
    input.sprint = (current_.input_flags & 0x80U) != 0U;
    input.hover = current_.hover;
    body_.orientation = current_.orientation;
    const auto movement_class =
        world::movement_config_for_class(current_.class_id, current_.movement_speed_scale);
    if (classic) static_cast<void>(world::step_classic_player(body_, input, map, dt, current_.hover, classic_jump_held_));
    else static_cast<void>(world::step_player(body_, input, map, dt, movement_class, {}, world_gravity));
    const auto finite = std::isfinite(body_.position.x) && std::isfinite(body_.position.y) &&
                        std::isfinite(body_.position.z);
    if (!finite) {
        reset(current_);
        return;
    }
    current_.position = body_.position;
    current_.velocity = body_.velocity;
}

void RemoteMotionInterpolator::push_classic(RemoteMotionSample sample, bool position_changed) noexcept {
    if (!initialized_) { reset(sample); return; }
    // Classic transmits no velocity. Continue simulation across position snaps,
    // and do not rewind position for an input/tool-only bridge update.
    const auto velocity = body_.velocity;
    const bool crouch = body_.crouch;
    if (!position_changed) sample.position = body_.position;
    push(sample, 0.1);
    body_.velocity = velocity;
    if (!position_changed) body_.crouch = crouch;
}

const RemoteMotionSample& RemoteMotionInterpolator::sample() const noexcept {
    return current_;
}

std::vector<CreatePlayerPacket> tutorial_create_player_fixtures() {
    return {
        {100U, false, 1U, 2U, false, 0U, {134.5F, 73.5F, 230.0F},
         {1.0F, 0.0F, 0.0F}, "Blue Soldier", {2U, 7U, 11U}, {}},
        {101U, false, 3U, 3U, false, 0U, {134.5F, 76.5F, 230.0F},
         {1.0F, 0.0F, 0.0F}, "Green Rocketeer", {2U, 9U, 12U}, {}},
        {102U, false, 4U, 2U, false, 0U, {134.5F, 79.5F, 230.0F},
         {1.0F, 0.0F, 0.0F}, "Blue Miner", {3U, 14U, 21U}, {}},
    };
}

std::vector<world::PlayerCollisionBody>
protocol168_collision_bodies(const Protocol168Roster& roster,
                             std::uint8_t local_player_id,
                             bool same_team_collision) {
    std::vector<world::PlayerCollisionBody> result;
    const auto* local = roster.player(local_player_id);
    if (local == nullptr || local->dead) return result;
    for (const auto& player : roster.players()) {
        if (player.player_id == local_player_id || player.dead ||
            (!same_team_collision && player.team == local->team)) {
            continue;
        }
        const bool crouch = (player.input_flags & 0x20U) != 0U;
        // WorldUpdate state bit 0x08 is "touching chemical goo", not water.
        // Wade is derived exactly as the movement core does: resting with
        // the origin below z=237 (the water plane).
        const bool wade = player.position.z > 237.0;
        result.push_back({player.position,
                          world::player_body_height(crouch, wade),
                          player.player_id});
    }
    return result;
}

} // namespace battlespades::network
