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

/** Encodes top-left-origin RGBA8 pixels as an in-memory PNG (packet 102 upload). */
[[nodiscard]] bool encode_png_rgba8(std::span<const std::uint8_t> rgba,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::vector<std::byte>& png_bytes,
                                    std::string& error) noexcept;

/**
 * Writes an already-encoded preview PNG as the matching local project's
 * `<uid>.png` sidecar (selected by title through the repository scanner).
 */
[[nodiscard]] bool write_ugc_preview_png(const std::filesystem::path& maps_root,
                                         std::string_view project_title,
                                         std::span<const std::byte> png_bytes,
                                         std::string& error) noexcept;

/**
 * ScreenshotHud.take_screenshot: the centred square of a top-left-origin RGBA8
 * frame, box-filtered down to 512x512 when larger (custom_image_scale).
 */
[[nodiscard]] std::vector<std::uint8_t> ugc_screenshot_preview_rgba(
    std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height,
    std::uint32_t& out_extent);

/**
 * Encodes top-left-origin RGBA8 pixels as a PNG at `path`, creating its
 * directory (GameScene.take_screenshot makes C:\AoS_Screenshots on demand).
 */
[[nodiscard]] bool write_screenshot_png(const std::filesystem::path& path,
                                        std::span<const std::uint8_t> rgba,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::string& error) noexcept;

} // namespace battlespades::frontend

