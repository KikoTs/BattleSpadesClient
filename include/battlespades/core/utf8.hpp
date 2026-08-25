#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace battlespades::core {

/**
 * Copies at most `maximum_code_points` complete UTF-8 scalar values.
 *
 * The function never splits a multi-byte sequence. A malformed sequence ends
 * the prefix so invalid bytes are not forwarded to text shaping. This mirrors
 * Python's code-point slicing for valid retail player names while failing
 * closed for malformed native configuration input.
 */
[[nodiscard]] std::string utf8_code_point_prefix(std::string_view value,
                                                 std::size_t maximum_code_points);

} // namespace battlespades::core
