#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace battlespades::network {
/** Decode bounded PNG/JPEG input into a 640x360 PNG, preserving its aspect ratio.
 * Invalid, oversized or unsupported input returns an empty vector. Worker-thread only. */
[[nodiscard]] std::vector<std::uint8_t> normalize_workshop_preview(std::span<const std::uint8_t> bytes);
}
