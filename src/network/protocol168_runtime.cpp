#include "battlespades/network/protocol168_runtime.hpp"

#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
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
            value |= static_cast<Unsigned>(
                         std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                     << (index * 8U);
        }
        offset_ += sizeof(Integer);
        return static_cast<Integer>(value);
    }
    [[nodiscard]] std::optional<float> f32() noexcept {
        const auto raw = integer<std::uint32_t>();
        if (!raw.has_value()) return std::nullopt;
        const float value = std::bit_cast<float>(*raw);
        return std::isfinite(value) ? std::optional<float>{value} : std::nullopt;
    }
    [[nodiscard]] std::optional<float> big_f32() noexcept {
        if (remaining() < sizeof(std::uint32_t)) return std::nullopt;
        std::uint32_t raw{};
        for (std::size_t index{}; index < sizeof(raw); ++index) {
            raw = (raw << 8U) |
                  std::to_integer<std::uint8_t>(bytes_[offset_ + index]);
        }
        offset_ += sizeof(raw);
        const float value = std::bit_cast<float>(raw);
        return std::isfinite(value) ? std::optional<float>{value} : std::nullopt;
    }
    [[nodiscard]] std::optional<std::vector<std::byte>>
    bytes(std::size_t count) {
        if (remaining() < count) return std::nullopt;
        std::vector<std::byte> result{
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + count)};
        offset_ += count;
        return result;
    }
    [[nodiscard]] std::optional<std::string> string(std::size_t maximum = 255U) {
        const auto begin = offset_;
        while (offset_ < bytes_.size() && bytes_[offset_] != std::byte{}) {
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
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
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
    [[nodiscard]] std::vector<std::byte> take() && {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

template <typename Value>
[[nodiscard]] bool required(std::optional<Value> input, Value& output) {
    if (!input.has_value()) return false;
    output = std::move(*input);
    return true;
}

[[nodiscard]] float from_fixed(std::int16_t raw) noexcept {
    const auto bits = static_cast<std::uint16_t>(raw);
    const float magnitude = static_cast<float>(bits & 0x7FFFU) / 64.0F;
    return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
}

[[nodiscard]] bool read_fixed(Reader& reader, float& output) noexcept {
    std::int16_t raw{};
    if (!required(reader.integer<std::int16_t>(), raw)) return false;
    output = from_fixed(raw);
    return true;
}

[[nodiscard]] bool read_fixed_vector(
    Reader& reader, std::array<float, 3U>& output) noexcept {
    for (auto& value : output) {
        if (!read_fixed(reader, value)) return false;
    }
    return true;
}

[[nodiscard]] RuntimeDecodeResult malformed(std::string message) {
    return {std::nullopt, std::move(message)};
}

template <typename Packet>
[[nodiscard]] RuntimeDecodeResult complete(Packet packet, const Reader& reader,
                                           const char* name) {
    if (!reader.done()) {
        return malformed(std::string{name} + " has trailing bytes");
    }
    return {RuntimePacket{std::move(packet)}, {}};
}

[[nodiscard]] bool safe_skybox_name(std::string_view value) noexcept {
    if (value.empty() || value.size() > 63U || !value.ends_with(".txt")) return false;
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != ' ' && character != '_' &&
            character != '-' && character != '.') {
            return false;
        }
    }
    return true;
}

template <typename Packet>
[[nodiscard]] bool read_playback_tail(Reader& reader, Packet& packet) noexcept {
    std::uint8_t flags{};
    if (!required(reader.u8(), flags) || !read_fixed(reader, packet.volume) ||
        !read_fixed(reader, packet.time)) {
        return false;
    }
    packet.looping = (flags & 0x01U) != 0U;
    packet.positioned = (flags & 0x02U) != 0U;
    if (packet.looping && !required(reader.u8(), packet.loop_id)) return false;
    return !packet.positioned ||
           (read_fixed_vector(reader, packet.position) &&
            read_fixed(reader, packet.attenuation));
}

} // namespace

RuntimeDecodeResult decode_runtime_packet(std::span<const std::byte> payload) {
    Reader reader{payload};
    const auto id = reader.u8();
    if (!id.has_value()) return malformed("empty runtime packet");

    if (*id == SetUgcEditModePacket::id || *id == UgcMessagePacket::id ||
        *id == UgcMapLoadingFromHostPacket::id) {
        const auto decoded = decode_ugc_control_packet(payload);
        if (!decoded) return malformed(decoded.error);
        if (const auto* mode = std::get_if<SetUgcEditModePacket>(&*decoded.packet)) {
            return {RuntimePacket{*mode}, {}};
        }
        if (const auto* message = std::get_if<UgcMessagePacket>(&*decoded.packet)) {
            return {RuntimePacket{*message}, {}};
        }
        if (const auto* progress =
                std::get_if<UgcMapLoadingFromHostPacket>(&*decoded.packet)) {
            return {RuntimePacket{*progress}, {}};
        }
        return malformed("server sent client-only ReqestUGCEntities(99)");
    }

    if (*id == ClockSyncPacket::id) {
        ClockSyncPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.client_time) ||
            !required(reader.integer<std::int32_t>(),
                      packet.server_loop_count)) {
            return malformed("malformed ClockSync(0)");
        }
        return complete(std::move(packet), reader, "ClockSync(0)");
    }
    if (*id == SetHpPacket::id) {
        SetHpPacket packet;
        if (!required(reader.u8(), packet.health) ||
            !required(reader.u8(), packet.damage_type) ||
            !read_fixed_vector(reader, packet.source)) {
            return malformed("malformed SetHP(5)");
        }
        return complete(std::move(packet), reader, "SetHP(5)");
    }
    if (*id == ChangeEntityPacket::id) {
        ChangeEntityPacket packet;
        if (!required(reader.integer<std::uint16_t>(), packet.entity_id) ||
            !required(reader.u8(), packet.action) ||
            packet.action > ChangeEntityPacket::set_ammo) {
            return malformed("malformed ChangeEntity(16) header");
        }
        switch (packet.action) {
        case ChangeEntityPacket::set_state:
            if (!required(reader.u8(), packet.state)) {
                return malformed("truncated ChangeEntity(16) state");
            }
            break;
        case ChangeEntityPacket::set_position:
            if (!read_fixed_vector(reader, packet.position)) {
                return malformed("truncated ChangeEntity(16) position");
            }
            break;
        case ChangeEntityPacket::set_velocity:
            if (!read_fixed_vector(reader, packet.velocity)) {
                return malformed("truncated ChangeEntity(16) velocity");
            }
            break;
        case ChangeEntityPacket::set_player:
            if (!required(reader.u8(), packet.player_id)) {
                return malformed("truncated ChangeEntity(16) player");
            }
            break;
        case ChangeEntityPacket::set_forward_vector:
            if (!read_fixed_vector(reader, packet.forward)) {
                return malformed("truncated ChangeEntity(16) forward vector");
            }
            break;
        case ChangeEntityPacket::set_target: {
            std::uint8_t target{};
            if (!required(reader.u8(), target)) {
                return malformed("truncated ChangeEntity(16) target");
            }
            packet.target_id = target < 128U
                                   ? static_cast<std::int16_t>(target)
                                   : static_cast<std::int16_t>(
                                         static_cast<std::int8_t>(target));
            break;
        }
        case ChangeEntityPacket::set_fuse:
            if (!read_fixed(reader, packet.fuse)) {
                return malformed("truncated ChangeEntity(16) fuse");
            }
            break;
        case ChangeEntityPacket::set_ammo:
            if (!read_fixed(reader, packet.ammo)) {
                return malformed("truncated ChangeEntity(16) ammo");
            }
            break;
        default:
            return malformed("unknown ChangeEntity(16) action");
        }
        return complete(std::move(packet), reader, "ChangeEntity(16)");
    }
    if (*id == ChangePlayerPacket::id) {
        ChangePlayerPacket packet;
        if (!required(reader.integer<std::uint16_t>(), packet.player_id) ||
            !required(reader.u8(), packet.type) || packet.type > 9U) {
            return malformed("malformed ChangePlayer(17) header");
        }
        if (packet.type == ChangePlayerPacket::set_high_minimap_visibility ||
            packet.type == ChangePlayerPacket::set_chase_cam) {
            std::uint8_t enabled{};
            if (!required(reader.u8(), enabled) || enabled > 1U) {
                return malformed("malformed ChangePlayer(17) boolean");
            }
            if (packet.type ==
                ChangePlayerPacket::set_high_minimap_visibility) {
                packet.high_minimap_visibility = enabled != 0U;
            } else {
                packet.chase_cam = enabled != 0U;
            }
        }
        return complete(std::move(packet), reader, "ChangePlayer(17)");
    }
    if (*id == HitEntityPacket::id) {
        HitEntityPacket packet;
        if (!required(reader.integer<std::uint16_t>(), packet.entity_id) ||
            !read_fixed_vector(reader, packet.position) ||
            !required(reader.u8(), packet.type)) {
            return malformed("malformed HitEntity(20)");
        }
        return complete(std::move(packet), reader, "HitEntity(20)");
    }
    if (*id == DestroyEntityPacket::id) {
        DestroyEntityPacket packet;
        if (!required(reader.integer<std::uint16_t>(), packet.entity_id)) {
            return malformed("malformed DestroyEntity(19)");
        }
        return complete(std::move(packet), reader, "DestroyEntity(19)");
    }
    if (*id == CreateEntityPacket::id) {
        CreateEntityPacket packet;
        if (!required(reader.integer<std::uint16_t>(), packet.entity_id) ||
            !required(reader.u8(), packet.type) ||
            !required(reader.u8(), packet.state) ||
            !required(reader.u8(), packet.player_id) ||
            !read_fixed_vector(reader, packet.position) ||
            !read_fixed_vector(reader, packet.velocity) ||
            !read_fixed(reader, packet.yaw) ||
            !read_fixed_vector(reader, packet.color) ||
            !read_fixed(reader, packet.radius) ||
            !required(reader.u8(), packet.face) ||
            !read_fixed(reader, packet.fuse)) {
            return malformed("malformed CreateEntity(21) prefix");
        }
        const auto integer_count = reader.u8();
        const auto float_count = reader.u8();
        if (!integer_count.has_value() || !float_count.has_value() ||
            !required(reader.u8(), packet.ugc_mode) || *integer_count > 64U ||
            *float_count > 64U) {
            return malformed("malformed CreateEntity(21) properties");
        }
        packet.integer_properties.reserve(*integer_count);
        for (std::size_t index{}; index < *integer_count; ++index) {
            const auto value = reader.integer<std::int32_t>();
            if (!value.has_value()) {
                return malformed("truncated CreateEntity(21) integer properties");
            }
            packet.integer_properties.push_back(*value);
        }
        packet.float_properties.reserve(*float_count);
        for (std::size_t index{}; index < *float_count; ++index) {
            float value{};
            if (!read_fixed(reader, value)) {
                return malformed("truncated CreateEntity(21) float properties");
            }
            packet.float_properties.push_back(value);
        }
        return complete(std::move(packet), reader, "CreateEntity(21)");
    }
    if (*id == PrefabCompletePacket::id) {
        return complete(PrefabCompletePacket{}, reader,
                        "PrefabComplete(29)");
    }
    if (*id == ExplodeCorpsePacket::id) {
        ExplodeCorpsePacket packet;
        std::uint8_t effect{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), effect) || effect > 1U) {
            return malformed("malformed ExplodeCorpse(36)");
        }
        packet.show_explosion_effect = effect != 0U;
        return complete(std::move(packet), reader, "ExplodeCorpse(36)");
    }
    if (*id == MinimapBillboardPacket::id) {
        MinimapBillboardPacket packet;
        std::uint8_t blue{};
        std::uint8_t green{};
        std::uint8_t red{};
        std::uint8_t tracking{};
        auto icon_name = std::optional<std::string>{};
        if (!required(reader.integer<std::uint16_t>(), packet.entity_id) ||
            !required(reader.u8(), packet.key) ||
            !required(reader.u8(), blue) || !required(reader.u8(), green) ||
            !required(reader.u8(), red) ||
            !read_fixed_vector(reader, packet.position) ||
            !(icon_name = reader.string(96U)).has_value() ||
            !required(reader.u8(), tracking) || tracking > 1U) {
            return malformed("malformed MinimapBillboard(41)");
        }
        packet.color = {red, green, blue};
        packet.icon_name = std::move(*icon_name);
        packet.tracking = tracking != 0U;
        return complete(std::move(packet), reader, "MinimapBillboard(41)");
    }
    if (*id == MinimapBillboardClearPacket::id) {
        MinimapBillboardClearPacket packet;
        if (!required(reader.integer<std::uint16_t>(), packet.entity_id)) {
            return malformed("malformed MinimapBillboardClear(42)");
        }
        return complete(std::move(packet), reader,
                        "MinimapBillboardClear(42)");
    }
    if (*id == MinimapZonePacket::id) {
        MinimapZonePacket packet;
        std::uint8_t blue{};
        std::uint8_t green{};
        std::uint8_t red{};
        std::uint8_t locked{};
        if (!required(reader.u8(), packet.key) ||
            !required(reader.u8(), blue) || !required(reader.u8(), green) ||
            !required(reader.u8(), red)) {
            return malformed("malformed MinimapZone(43) header");
        }
        // shared.packet writes A2018,A2020,A2022,A2019,A2021,A2023:
        // those aliases are x1,y1,z1,x2,y2,z2, not axis-paired bounds.
        for (auto& coordinate : packet.minimum) {
            if (!required(reader.integer<std::int16_t>(), coordinate)) {
                return malformed("truncated MinimapZone(43) minimum");
            }
        }
        for (auto& coordinate : packet.maximum) {
            if (!required(reader.integer<std::int16_t>(), coordinate)) {
                return malformed("truncated MinimapZone(43) maximum");
            }
        }
        if (!read_fixed(reader, packet.icon_scale) ||
            !required(reader.u8(), packet.icon_id) ||
            !required(reader.u8(), locked) || locked > 1U) {
            return malformed("malformed MinimapZone(43) tail");
        }
        packet.color = {red, green, blue};
        packet.locked_in_zone = locked != 0U;
        return complete(std::move(packet), reader, "MinimapZone(43)");
    }
    if (*id == MinimapZoneClearPacket::id) {
        MinimapZoneClearPacket packet;
        for (auto& coordinate : packet.minimum) {
            if (!required(reader.integer<std::int16_t>(), coordinate)) {
                return malformed("truncated MinimapZoneClear(44) minimum");
            }
        }
        for (auto& coordinate : packet.maximum) {
            if (!required(reader.integer<std::int16_t>(), coordinate)) {
                return malformed("truncated MinimapZoneClear(44) maximum");
            }
        }
        return complete(std::move(packet), reader, "MinimapZoneClear(44)");
    }
    if (*id == LockToZonePacket::id) {
        LockToZonePacket packet;
        for (auto& coordinate : packet.minimum) {
            if (!required(reader.integer<std::int16_t>(), coordinate)) {
                return malformed("truncated LockToZone(108) minimum");
            }
        }
        for (auto& coordinate : packet.maximum) {
            if (!required(reader.integer<std::int16_t>(), coordinate)) {
                return malformed("truncated LockToZone(108) maximum");
            }
        }
        if (packet.minimum[0U] > packet.maximum[0U] ||
            packet.minimum[1U] > packet.maximum[1U] ||
            packet.minimum[2U] > packet.maximum[2U]) {
            return malformed("malformed LockToZone(108) bounds");
        }
        return complete(std::move(packet), reader, "LockToZone(108)");
    }
    if (*id == CreateAmbientSoundPacket::id) {
        CreateAmbientSoundPacket packet;
        auto name = reader.string();
        std::uint8_t count{};
        if (!name.has_value() || !required(reader.u8(), packet.loop_id) ||
            !required(reader.u8(), count) || count > 64U) {
            return malformed("malformed CreateAmbientSound(22)");
        }
        packet.name = std::move(*name);
        packet.points.reserve(count);
        for (std::size_t index{}; index < count; ++index) {
            std::array<std::int16_t, 3U> point{};
            for (auto& coordinate : point) {
                if (!required(reader.integer<std::int16_t>(), coordinate)) {
                    return malformed("truncated CreateAmbientSound(22) points");
                }
            }
            packet.points.push_back(point);
        }
        return complete(std::move(packet), reader, "CreateAmbientSound(22)");
    }
    if (*id == PlaySoundPacket::id) {
        PlaySoundPacket packet;
        if (!required(reader.u8(), packet.sound_id) ||
            !read_playback_tail(reader, packet)) {
            return malformed("malformed PlaySound(23)");
        }
        return complete(std::move(packet), reader, "PlaySound(23)");
    }
    if (*id == PlayAmbientSoundPacket::id) {
        PlayAmbientSoundPacket packet;
        auto name = reader.string();
        if (!name.has_value() || !read_playback_tail(reader, packet)) {
            return malformed("malformed PlayAmbientSound(24)");
        }
        packet.name = std::move(*name);
        return complete(std::move(packet), reader, "PlayAmbientSound(24)");
    }
    if (*id == StopSoundPacket::id) {
        StopSoundPacket packet;
        if (!required(reader.u8(), packet.loop_id)) {
            return malformed("malformed StopSound(25)");
        }
        return complete(packet, reader, "StopSound(25)");
    }
    if (*id == PlayMusicPacket::id) {
        PlayMusicPacket packet;
        auto name = reader.string();
        if (!name.has_value() || !read_fixed(reader, packet.seconds_played)) {
            return malformed("malformed PlayMusic(26)");
        }
        packet.name = std::move(*name);
        return complete(std::move(packet), reader, "PlayMusic(26)");
    }
    if (*id == StopMusicPacket::id) {
        return complete(StopMusicPacket{}, reader, "StopMusic(27)");
    }
    if (*id == PlayerLeftPacket::id) {
        PlayerLeftPacket packet;
        if (!required(reader.u8(), packet.player_id)) {
            return malformed("malformed PlayerLeft(64)");
        }
        return complete(std::move(packet), reader, "PlayerLeft(64)");
    }
    if (*id == UgcObjectivesPacket::id) {
        UgcObjectivesPacket packet;
        std::int32_t count{};
        if (!required(reader.u8(), packet.mode) ||
            !required(reader.integer<std::int32_t>(), count) ||
            count < 0 || count > 128) {
            return malformed("malformed UGCObjectives(68) header");
        }
        packet.objectives.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index{}; index < count; ++index) {
            auto objective = reader.string(128U);
            std::int32_t value{};
            if (!objective.has_value() ||
                !required(reader.integer<std::int32_t>(), value)) {
                return malformed("truncated UGCObjectives(68) rows");
            }
            packet.objectives.push_back(
                UgcObjective{std::move(*objective), value});
        }
        return complete(std::move(packet), reader, "UGCObjectives(68)");
    }
    if (*id == PickPickupPacket::id) {
        PickPickupPacket packet;
        std::uint8_t burdensome{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.pickup_id) ||
            !required(reader.u8(), burdensome) || burdensome > 1U) {
            return malformed("malformed PickPickup(70)");
        }
        packet.burdensome = burdensome != 0U;
        return complete(std::move(packet), reader, "PickPickup(70)");
    }
    if (*id == FogColorPacket::id) {
        FogColorPacket packet;
        std::uint32_t wire{};
        if (!required(reader.integer<std::uint32_t>(), wire) ||
            (wire & 0xFFU) != 0U) {
            return malformed("malformed FogColor(74)");
        }
        packet.color = {
            static_cast<std::uint8_t>(wire >> 24U),
            static_cast<std::uint8_t>(wire >> 16U),
            static_cast<std::uint8_t>(wire >> 8U),
        };
        return complete(std::move(packet), reader, "FogColor(74)");
    }
    if (*id == InitialUgcBatchPacket::id) {
        InitialUgcBatchPacket packet;
        std::int32_t count{};
        if (!required(reader.integer<std::int32_t>(), count) ||
            count < 0 || count > 65'536) {
            return malformed("malformed InitialUGCBatch(98) count");
        }
        packet.items.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index{}; index < count; ++index) {
            InitialUgcItem item;
            if (!required(reader.u8(), item.mode)) {
                return malformed("truncated InitialUGCBatch(98) mode");
            }
            for (auto& coordinate : item.position) {
                if (!required(reader.integer<std::int16_t>(), coordinate)) {
                    return malformed(
                        "truncated InitialUGCBatch(98) position");
                }
            }
            if (!required(reader.u8(), item.item_id)) {
                return malformed("truncated InitialUGCBatch(98) item");
            }
            packet.items.push_back(item);
        }
        return complete(std::move(packet), reader, "InitialUGCBatch(98)");
    }
    if (*id == UgcMapInfoPacket::id) {
        UgcMapInfoPacket packet;
        std::int32_t length{};
        if (!required(reader.integer<std::int32_t>(), length) ||
            length < 0 || length > 8 * 1024 * 1024 ||
            reader.remaining() != static_cast<std::size_t>(length)) {
            return malformed("malformed UGCMapInfo(102) length");
        }
        auto png = reader.bytes(static_cast<std::size_t>(length));
        if (!png.has_value() ||
            (!png->empty() &&
             (png->size() < 8U ||
              (*png)[0U] != std::byte{0x89U} ||
              (*png)[1U] != std::byte{'P'} ||
              (*png)[2U] != std::byte{'N'} ||
              (*png)[3U] != std::byte{'G'}))) {
            return malformed("UGCMapInfo(102) is not a PNG");
        }
        packet.png_data = std::move(*png);
        return complete(std::move(packet), reader, "UGCMapInfo(102)");
    }
    if (*id == HelpMessagePacket::id) {
        HelpMessagePacket packet;
        std::uint8_t count{};
        if (!required(reader.big_f32(), packet.delay) ||
            packet.delay < 0.0F || packet.delay > 120.0F ||
            !required(reader.u8(), count) || count > 32U) {
            return malformed("malformed HelpMessage(109) header");
        }
        packet.message_ids.reserve(count);
        for (std::size_t index{}; index < count; ++index) {
            auto message = reader.string(128U);
            if (!message.has_value()) {
                return malformed("truncated HelpMessage(109) strings");
            }
            if (!message->empty()) {
                packet.message_ids.push_back(std::move(*message));
            }
        }
        return complete(std::move(packet), reader, "HelpMessage(109)");
    }
    if (*id == SetGroundColorsPacket::id) {
        SetGroundColorsPacket packet;
        std::uint8_t count{};
        if (!required(reader.u8(), count)) {
            return malformed("malformed SetGroundColors(118) count");
        }
        packet.colors.reserve(count);
        for (std::size_t index{}; index < count; ++index) {
            std::array<std::uint8_t, 4U> color{};
            for (auto& channel : color) {
                if (!required(reader.u8(), channel)) {
                    return malformed(
                        "truncated SetGroundColors(118) palette");
                }
            }
            packet.colors.push_back(color);
        }
        return complete(std::move(packet), reader,
                        "SetGroundColors(118)");
    }
    if (*id == SkyboxDataPacket::id) {
        SkyboxDataPacket packet;
        if (!required(reader.string(63U), packet.definition_name) ||
            !safe_skybox_name(packet.definition_name)) {
            return malformed("malformed or unsafe SkyboxData(51) definition name");
        }
        return complete(std::move(packet), reader, "SkyboxData(51)");
    }
    if (*id == DisplayCountdownPacket::id) {
        DisplayCountdownPacket packet;
        if (!required(reader.f32(), packet.timer)) {
            return malformed("malformed DisplayCountdown(84)");
        }
        return complete(std::move(packet), reader, "DisplayCountdown(84)");
    }
    if (*id == LockTeamPacket::id) {
        LockTeamPacket packet;
        std::uint8_t locked{};
        if (!required(reader.u8(), packet.team_id) ||
            !required(reader.u8(), locked) || locked > 1U ||
            (packet.team_id != 0U && packet.team_id != 2U && packet.team_id != 3U)) {
            return malformed("malformed LockTeam(79)");
        }
        packet.locked = locked != 0U;
        return complete(std::move(packet), reader, "LockTeam(79)");
    }
    if (*id == TeamLockClassPacket::id) {
        TeamLockClassPacket packet;
        std::uint8_t locked{};
        if (!required(reader.u8(), packet.team_id) ||
            !required(reader.u8(), locked) || locked > 1U ||
            (packet.team_id != 2U && packet.team_id != 3U)) {
            return malformed("malformed TeamLockClass(80)");
        }
        packet.locked = locked != 0U;
        return complete(std::move(packet), reader, "TeamLockClass(80)");
    }
    if (*id == TeamLockScorePacket::id) {
        TeamLockScorePacket packet;
        std::uint8_t locked{};
        if (!required(reader.u8(), packet.team_id) ||
            !required(reader.u8(), locked) || locked > 1U ||
            (packet.team_id != 2U && packet.team_id != 3U)) {
            return malformed("malformed TeamLockScore(81)");
        }
        packet.locked = locked != 0U;
        return complete(std::move(packet), reader, "TeamLockScore(81)");
    }
    if (*id == TeamInfiniteBlocksPacket::id) {
        TeamInfiniteBlocksPacket packet;
        std::uint8_t infinite{};
        if (!required(reader.u8(), packet.team_id) ||
            !required(reader.u8(), infinite) || infinite > 1U ||
            (packet.team_id != 2U && packet.team_id != 3U)) {
            return malformed("malformed TeamInfiniteBlocks(82)");
        }
        packet.infinite = infinite != 0U;
        return complete(std::move(packet), reader, "TeamInfiniteBlocks(82)");
    }
    if (*id == TeamMapVisibilityPacket::id) {
        TeamMapVisibilityPacket packet;
        std::uint8_t visible{};
        if (!required(reader.u8(), packet.team_id) ||
            !required(reader.u8(), visible) || visible > 1U ||
            (packet.team_id != 2U && packet.team_id != 3U)) {
            return malformed("malformed TeamMapVisibility(83)");
        }
        packet.visible = visible != 0U;
        return complete(std::move(packet), reader,
                        "TeamMapVisibility(83)");
    }
    if (*id == SetScorePacket::id) {
        SetScorePacket packet;
        if (!required(reader.u8(), packet.type) ||
            !required(reader.u8(), packet.reason) ||
            !required(reader.u8(), packet.specifier) ||
            !required(reader.integer<std::int32_t>(), packet.value) ||
            packet.type > 1U) {
            return malformed("malformed SetScore(85)");
        }
        return complete(std::move(packet), reader, "SetScore(85)");
    }
    if (*id == KillActionPacket::id) {
        KillActionPacket packet;
        std::uint8_t domination{};
        std::uint8_t revenge{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.killer_id) ||
            !required(reader.u8(), packet.kill_type) ||
            !required(reader.u8(), packet.respawn_time) ||
            !required(reader.u8(), packet.kill_count) ||
            !required(reader.u8(), domination) ||
            !required(reader.u8(), revenge) ||
            domination > 1U || revenge > 1U) {
            return malformed("malformed KillAction(46)");
        }
        packet.domination = domination != 0U;
        packet.revenge = revenge != 0U;
        return complete(std::move(packet), reader, "KillAction(46)");
    }
    if (*id == GenericVoteMessagePacket::id) {
        GenericVoteMessagePacket packet;
        std::uint16_t count{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.message_type) ||
            packet.message_type > GenericVoteMessagePacket::closed ||
            !required(reader.integer<std::uint16_t>(), count) || count > 3U) {
            return malformed("malformed GenericVoteMessage(47) header");
        }
        packet.candidates.reserve(count);
        for (std::uint16_t index{}; index < count; ++index) {
            auto name = reader.string(128U);
            std::int32_t votes{};
            if (!name.has_value() ||
                !required(reader.integer<std::int32_t>(), votes)) {
                return malformed(
                    "truncated GenericVoteMessage(47) candidates");
            }
            packet.candidates.push_back(
                GenericVoteCandidate{std::move(*name), votes});
        }
        auto title = reader.string(256U);
        auto description = reader.string(256U);
        std::uint8_t flags{};
        if (!title.has_value() || !description.has_value() ||
            !required(reader.u8(), flags) || (flags & 0xF8U) != 0U) {
            return malformed("malformed GenericVoteMessage(47) tail");
        }
        packet.title = std::move(*title);
        packet.description = std::move(*description);
        packet.allow_revote = (flags & 0x01U) != 0U;
        packet.hide_after_vote = (flags & 0x02U) != 0U;
        packet.can_vote = (flags & 0x04U) != 0U;
        return complete(std::move(packet), reader, "GenericVoteMessage(47)");
    }
    if (*id == ChatMessagePacket::id) {
        ChatMessagePacket packet;
        auto value = std::optional<std::string>{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.chat_type) ||
            !(value = reader.string(256U)).has_value()) {
            return malformed("malformed ChatMessage(49)");
        }
        packet.value = std::move(*value);
        return complete(std::move(packet), reader, "ChatMessage(49)");
    }
    if (*id == LocalisedMessagePacket::id) {
        LocalisedMessagePacket packet;
        std::uint8_t localise_parameters{};
        std::uint8_t parameter_count{};
        std::uint8_t override_previous{};
        auto string_id = std::optional<std::string>{};
        if (!required(reader.u8(), packet.chat_type) ||
            !required(reader.u8(), localise_parameters) ||
            localise_parameters > 1U ||
            !(string_id = reader.string(256U)).has_value() ||
            !required(reader.u8(), parameter_count) ||
            parameter_count > 16U) {
            return malformed("malformed LocalisedMessage(50) header");
        }
        packet.string_id = std::move(*string_id);
        packet.localise_parameters = localise_parameters != 0U;
        packet.parameters.reserve(parameter_count);
        for (std::size_t index{}; index < parameter_count; ++index) {
            auto parameter = reader.string(256U);
            if (!parameter.has_value()) {
                return malformed("truncated LocalisedMessage(50) parameters");
            }
            packet.parameters.push_back(std::move(*parameter));
        }
        if (!required(reader.u8(), override_previous) ||
            override_previous > 1U) {
            return malformed("malformed LocalisedMessage(50) override flag");
        }
        packet.override_previous_message = override_previous != 0U;
        return complete(std::move(packet), reader, "LocalisedMessage(50)");
    }
    if (*id == MapEndedPacket::id) {
        return complete(MapEndedPacket{}, reader, "MapEnded(52)");
    }
    if (*id == ShowGameStatsPacket::id) {
        return complete(ShowGameStatsPacket{}, reader, "ShowGameStats(53)");
    }
    if (*id == RankUpsPacket::id) {
        RankUpsPacket packet;
        std::int32_t count{};
        if (!required(reader.integer<std::int32_t>(), count) || count < 0 || count > 251) {
            return malformed("malformed RankUps(66) count");
        }
        packet.entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index{}; index < count; ++index) {
            RankUpEntry entry;
            if (!required(reader.integer<std::int32_t>(), entry.score_reason) ||
                entry.score_reason < 0 || entry.score_reason > 250) {
                return malformed("malformed RankUps(66) reason");
            }
            const auto old_text = reader.string(32U);
            const auto new_text = reader.string(32U);
            if (!old_text.has_value() || !new_text.has_value()) {
                return malformed("malformed RankUps(66) entry");
            }
            const auto parse_score = [](std::string_view text,
                                        std::int64_t& destination) noexcept {
                const auto result = std::from_chars(
                    text.data(), text.data() + text.size(), destination);
                return result.ec == std::errc{} &&
                       result.ptr == text.data() + text.size() && destination >= 0;
            };
            if (!parse_score(*old_text, entry.old_score) ||
                !parse_score(*new_text, entry.new_score) ||
                entry.new_score < entry.old_score) {
                return malformed("malformed RankUps(66) score");
            }
            packet.entries.push_back(entry);
        }
        return complete(std::move(packet), reader, "RankUps(66)");
    }
    if (*id == GameStatsPacket::id) {
        GameStatsPacket packet;
        std::int32_t count{};
        if (!required(reader.integer<std::int32_t>(), count) ||
            !required(reader.integer<std::int32_t>(), packet.team_id) ||
            count < 0 || count > 128 ||
            packet.team_id < 0 || packet.team_id > 3) {
            return malformed("malformed GameStats(67) header");
        }
        packet.entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index{}; index < count; ++index) {
            GameStatEntry entry;
            if (!required(reader.integer<std::int32_t>(), entry.player_id) ||
                !required(reader.integer<std::int32_t>(), entry.stat_type) ||
                entry.player_id < 0 || entry.player_id >= 128 ||
                entry.stat_type < 0 || entry.stat_type > 29) {
                return malformed("malformed GameStats(67) entry");
            }
            packet.entries.push_back(entry);
        }
        return complete(std::move(packet), reader, "GameStats(67)");
    }
    if (*id == ForceShowScoresPacket::id) {
        ForceShowScoresPacket packet;
        std::uint8_t forced{};
        if (!required(reader.u8(), forced) || forced > 1U) {
            return malformed("malformed ForceShowScores(72) flag");
        }
        packet.forced = forced != 0U;
        return complete(packet, reader, "ForceShowScores(72)");
    }
    if (*id == ShowTextMessagePacket::id) {
        ShowTextMessagePacket packet;
        if (!required(reader.u8(), packet.message_id) ||
            packet.message_id > 8U || !read_fixed(reader, packet.duration) ||
            !std::isfinite(packet.duration)) {
            return malformed("malformed ShowTextMessage(73)");
        }
        return complete(packet, reader, "ShowTextMessage(73)");
    }
    if (*id == TeamProgressPacket::id) {
        TeamProgressPacket packet;
        std::uint8_t flags{};
        if (!required(reader.u8(), packet.team_id) ||
            !required(reader.u8(), flags) || (flags & 0xF0U) != 0U) {
            return malformed("malformed TeamProgress(117) header");
        }
        packet.visible = (flags & 0x01U) != 0U;
        packet.show_particle = (flags & 0x02U) != 0U;
        packet.show_previous = (flags & 0x04U) != 0U;
        packet.show_as_percent = (flags & 0x08U) != 0U;
        if (packet.show_as_percent) {
            if (!read_fixed(reader, packet.percent)) {
                return malformed("malformed TeamProgress(117) percent");
            }
        } else if (!required(reader.integer<std::int32_t>(),
                             packet.numerator) ||
                   !required(reader.integer<std::int32_t>(),
                             packet.denominator)) {
            return malformed("malformed TeamProgress(117) fraction");
        }
        if (!required(reader.u8(), packet.icon_id)) {
            return malformed("malformed TeamProgress(117) icon");
        }
        return complete(std::move(packet), reader, "TeamProgress(117)");
    }
    if (*id == TerritoryBaseStatePacket::id) {
        TerritoryBaseStatePacket packet;
        if (!required(reader.u8(), packet.base_index) ||
            !required(reader.u8(), packet.action) ||
            !required(reader.u8(), packet.controlled_by) ||
            !required(reader.u8(), packet.attacked_by) ||
            !read_fixed(reader, packet.capture_amount) ||
            packet.action > 7U || packet.controlled_by > 3U ||
            packet.attacked_by > 3U) {
            return malformed("malformed TerritoryBaseState(106)");
        }
        return complete(std::move(packet), reader,
                        "TerritoryBaseState(106)");
    }
    return malformed("packet is not a supported runtime packet");
}

std::vector<std::byte> encode_packet(const ClockSyncPacket& packet) {
    Writer writer;
    writer.u8(ClockSyncPacket::id);
    writer.integer(packet.client_time);
    writer.integer(packet.server_loop_count);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const ChatMessagePacket& packet) {
    if (packet.chat_type > 3U || packet.value.empty() ||
        packet.value.size() > 200U ||
        packet.value.find('\0') != std::string::npos) {
        return {};
    }
    Writer writer;
    writer.u8(ChatMessagePacket::id);
    writer.u8(packet.player_id);
    writer.u8(packet.chat_type);
    writer.string(packet.value);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const SkyboxDataPacket& packet) {
    if (!safe_skybox_name(packet.definition_name)) return {};
    Writer writer;
    writer.u8(SkyboxDataPacket::id);
    writer.string(packet.definition_name);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const SetGroundColorsPacket& packet) {
    if (packet.colors.size() > std::numeric_limits<std::uint8_t>::max()) return {};
    Writer writer;
    writer.u8(SetGroundColorsPacket::id);
    writer.u8(static_cast<std::uint8_t>(packet.colors.size()));
    for (const auto& color : packet.colors) {
        for (const auto channel : color) writer.u8(channel);
    }
    return std::move(writer).take();
}

std::vector<std::byte> encode_generic_vote_cast(
    std::uint8_t player_id, std::string_view candidate) {
    if (candidate.empty() || candidate.size() > 128U ||
        candidate.find('\0') != std::string_view::npos) {
        return {};
    }
    Writer writer;
    writer.u8(GenericVoteMessagePacket::id);
    writer.u8(player_id);
    writer.u8(GenericVoteMessagePacket::cast);
    writer.integer<std::uint16_t>(1U);
    // The server compares this opaque token with the candidate token it sent.
    // Decoding it for display and re-encoding it would make the vote invalid.
    writer.string(candidate);
    writer.integer<std::int32_t>(0);
    writer.string("");
    writer.string("");
    writer.u8(0U);
    return std::move(writer).take();
}

} // namespace battlespades::network
