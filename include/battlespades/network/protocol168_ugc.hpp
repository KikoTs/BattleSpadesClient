#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace battlespades::network {

/**
 * Exact InitialInfo MapIsUGC byte recovered from the retail client.
 *
 * This is a role, not a boolean: the host owns editor settings/save requests,
 * while a client is a collaborative builder. Collapsing 1 and 2 loses that
 * authority boundary and makes the stock UGC menus choose the wrong path.
 */
enum class UgcRole : std::uint8_t {
    none = 0U,
    host = 1U,
    client = 2U,
};

[[nodiscard]] constexpr bool is_ugc(UgcRole role) noexcept {
    return role != UgcRole::none;
}

/** Server-owned editor target-mode mutation. */
struct SetUgcEditModePacket final {
    static constexpr std::uint8_t id{12U};
    std::uint8_t mode{};
    [[nodiscard]] friend bool operator==(const SetUgcEditModePacket&,
                                         const SetUgcEditModePacket&) = default;
};

/** Client request for the complete mode-filtered UGC object collection. */
struct RequestUgcEntitiesPacket final {
    // Retail preserves the misspelling `ReqestUGCEntities` in its class name.
    static constexpr std::uint8_t id{99U};
    std::uint8_t game_mode{};
    bool in_ugc_mode{};
    [[nodiscard]] friend bool operator==(const RequestUgcEntitiesPacket&,
                                         const RequestUgcEntitiesPacket&) = default;
};

enum class UgcMessageCode : std::uint8_t {
    convert_to_game = 0U,
    request_map_validation = 1U,
    request_vxl = 2U,
    no_vxl_use_baseplate = 3U,
    request_map_info = 4U,
};

/** Bidirectional one-byte editor lifecycle command. */
struct UgcMessagePacket final {
    static constexpr std::uint8_t id{100U};
    UgcMessageCode message{UgcMessageCode::convert_to_game};
    [[nodiscard]] friend bool operator==(const UgcMessagePacket&,
                                         const UgcMessagePacket&) = default;
};

/** Host source-map loading progress shown to collaborative members. */
struct UgcMapLoadingFromHostPacket final {
    static constexpr std::uint8_t id{101U};
    std::uint8_t percent{};
    [[nodiscard]] friend bool operator==(const UgcMapLoadingFromHostPacket&,
                                         const UgcMapLoadingFromHostPacket&) = default;
};

using UgcControlPacket =
    std::variant<SetUgcEditModePacket,
                 RequestUgcEntitiesPacket,
                 UgcMessagePacket,
                 UgcMapLoadingFromHostPacket>;

struct UgcControlDecodeResult final {
    std::optional<UgcControlPacket> packet;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return packet.has_value(); }
};

/** Strict codec shared by the handshake, runtime dispatcher and parity tests. */
[[nodiscard]] UgcControlDecodeResult
decode_ugc_control_packet(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_packet(const SetUgcEditModePacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const RequestUgcEntitiesPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const UgcMessagePacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const UgcMapLoadingFromHostPacket& packet);

} // namespace battlespades::network
