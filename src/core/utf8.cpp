#include "battlespades/core/utf8.hpp"

#include <cstdint>

namespace battlespades::core {
namespace {

[[nodiscard]] std::size_t decoded_scalar_byte_count(std::string_view value,
                                                    std::size_t offset) noexcept {
    const auto lead = static_cast<std::uint8_t>(value[offset]);
    std::uint32_t scalar{};
    std::uint32_t minimum{};
    std::size_t byte_count{};
    if (lead <= 0x7FU) {
        return 1U;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        scalar = lead & 0x1FU;
        minimum = 0x80U;
        byte_count = 2U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        scalar = lead & 0x0FU;
        minimum = 0x800U;
        byte_count = 3U;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        scalar = lead & 0x07U;
        minimum = 0x1'0000U;
        byte_count = 4U;
    } else {
        return 0U;
    }

    if (byte_count > value.size() - offset) {
        return 0U;
    }
    for (std::size_t index = 1U; index < byte_count; ++index) {
        const auto continuation = static_cast<std::uint8_t>(value[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            return 0U;
        }
        scalar = (scalar << 6U) | (continuation & 0x3FU);
    }

    if (scalar < minimum || scalar > 0x10'FFFFU || (scalar >= 0xD800U && scalar <= 0xDFFFU)) {
        return 0U;
    }
    return byte_count;
}

} // namespace

std::string utf8_code_point_prefix(std::string_view value, std::size_t maximum_code_points) {
    std::size_t offset{};
    std::size_t code_points{};
    while (offset < value.size() && code_points < maximum_code_points) {
        const auto byte_count = decoded_scalar_byte_count(value, offset);
        if (byte_count == 0U) {
            break;
        }
        offset += byte_count;
        ++code_points;
    }
    return std::string{value.substr(0U, offset)};
}

} // namespace battlespades::core
