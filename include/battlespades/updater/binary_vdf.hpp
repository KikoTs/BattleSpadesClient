#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/// Value tags of Valve's binary KeyValues, as used by shortcuts.vdf.
enum class BinaryVdfType : std::uint8_t {
    map = 0x00,
    string = 0x01,
    int32 = 0x02,
    float32 = 0x03,
    pointer = 0x04,
    color = 0x06,
    uint64 = 0x07,
    end = 0x08,
    int64 = 0x0A,
};

/**
 * One binary KeyValues entry. Fixed-size numeric payloads are kept as raw
 * little-endian bytes so an untouched entry is re-serialised byte-for-byte.
 */
struct BinaryVdfNode {
    BinaryVdfType type{BinaryVdfType::string};
    std::string key;
    std::string text;                 ///< string payload
    std::vector<std::uint8_t> raw;    ///< numeric payload
    std::vector<BinaryVdfNode> children;

    [[nodiscard]] static BinaryVdfNode make_map(std::string key);
    [[nodiscard]] static BinaryVdfNode make_string(std::string key, std::string value);
    [[nodiscard]] static BinaryVdfNode make_int32(std::string key, std::uint32_t value);

    [[nodiscard]] std::optional<std::uint32_t> as_uint32() const noexcept;
    [[nodiscard]] BinaryVdfNode* child(std::string_view name);
    [[nodiscard]] const BinaryVdfNode* child(std::string_view name) const;
};

/// Parses a whole file: top-level entries terminated by one 0x08.
[[nodiscard]] std::optional<std::vector<BinaryVdfNode>>
parse_binary_vdf(std::span<const std::uint8_t> bytes, std::string& error);
[[nodiscard]] std::vector<std::uint8_t> serialize_binary_vdf(const std::vector<BinaryVdfNode>& roots);

} // namespace battlespades::updater
