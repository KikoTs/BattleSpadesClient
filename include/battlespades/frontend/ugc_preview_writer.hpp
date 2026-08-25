#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/**
 * Encodes a retail 512-square UGC overhead preview and atomically replaces the
 * matching local project's PNG sidecar.
 *
 * The project is selected by its authored title through the bounded repository
 * scanner; callers never turn a title into a path. `rgba` is top-left-origin,
 * tightly packed RGBA8. On success `png_bytes` contains the exact persisted
 * payload for immediate loading-screen use.
 */
[[nodiscard]] bool write_ugc_overhead_preview(
    const std::filesystem::path& maps_root,
    std::string_view project_title,
    std::span<const std::uint8_t> rgba,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::byte>& png_bytes,
    std::string& error) noexcept;

} // namespace battlespades::frontend

