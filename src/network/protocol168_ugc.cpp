#include "battlespades/network/protocol168_ugc.hpp"

namespace battlespades::network {
namespace {

[[nodiscard]] std::uint8_t byte_at(std::span<const std::byte> bytes,
                                   std::size_t index) noexcept {
    return std::to_integer<std::uint8_t>(bytes[index]);
}

template <typename Packet>
[[nodiscard]] std::vector<std::byte> two_bytes(std::uint8_t value) {
    return {static_cast<std::byte>(Packet::id), static_cast<std::byte>(value)};
}

} // namespace

UgcControlDecodeResult decode_ugc_control_packet(std::span<const std::byte> bytes) {
    if (bytes.empty()) return {{}, "empty UGC control packet"};
    const auto id = byte_at(bytes, 0U);
    if (id == SetUgcEditModePacket::id) {
        if (bytes.size() != 2U) {
            return {{}, "SetUGCEditMode(12) must contain exactly one mode byte"};
        }
        return {UgcControlPacket{SetUgcEditModePacket{byte_at(bytes, 1U)}}, {}};
    }
    if (id == RequestUgcEntitiesPacket::id) {
        if (bytes.size() != 3U || byte_at(bytes, 2U) > 1U) {
            return {{}, "ReqestUGCEntities(99) has a noncanonical UGC flag"};
        }
        return {UgcControlPacket{RequestUgcEntitiesPacket{
                    byte_at(bytes, 1U), byte_at(bytes, 2U) != 0U}},
                {}};
    }
    if (id == UgcMessagePacket::id) {
        if (bytes.size() != 2U || byte_at(bytes, 1U) > 4U) {
            return {{}, "UGCMessage(100) has an unknown lifecycle command"};
        }
        return {UgcControlPacket{UgcMessagePacket{
                    static_cast<UgcMessageCode>(byte_at(bytes, 1U))}},
                {}};
    }
    if (id == UgcMapLoadingFromHostPacket::id) {
        if (bytes.size() != 2U || byte_at(bytes, 1U) > 100U) {
            return {{}, "UGCMapLoadingFromHost(101) percent exceeds 100"};
        }
        return {UgcControlPacket{UgcMapLoadingFromHostPacket{byte_at(bytes, 1U)}}, {}};
    }
    return {{}, "packet is not a UGC control packet"};
}

std::vector<std::byte> encode_packet(const SetUgcEditModePacket& packet) {
    return two_bytes<SetUgcEditModePacket>(packet.mode);
}

std::vector<std::byte> encode_packet(const RequestUgcEntitiesPacket& packet) {
    return {static_cast<std::byte>(RequestUgcEntitiesPacket::id),
            static_cast<std::byte>(packet.game_mode),
            static_cast<std::byte>(packet.in_ugc_mode ? 1U : 0U)};
}

std::vector<std::byte> encode_packet(const UgcMessagePacket& packet) {
    return two_bytes<UgcMessagePacket>(static_cast<std::uint8_t>(packet.message));
}

std::vector<std::byte> encode_packet(const UgcMapLoadingFromHostPacket& packet) {
    if (packet.percent > 100U) return {};
    return two_bytes<UgcMapLoadingFromHostPacket>(packet.percent);
}

} // namespace battlespades::network
