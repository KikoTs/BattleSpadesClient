#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace battlespades::render {

/** The three texture roots selected by retail aosimage.set_texture_quality. */
enum class TextureQualityTier : std::uint8_t {
    low,
    medium,
    high,
};

/** Returns the source directory spelling below png/: low, med, or high. */
[[nodiscard]] constexpr std::string_view
texture_quality_directory(TextureQualityTier quality) noexcept {
    switch (quality) {
    case TextureQualityTier::low:
        return "low";
    case TextureQualityTier::medium:
        return "med";
    case TextureQualityTier::high:
        return "high";
    }
    return "med";
}

/**
 * Applies retail's quality resource-root selection to one native asset key.
 *
 * Native draw lists use an explicit `png/high/<name>` spelling because they
 * cannot mutate pyglet.resource.path. Retail instead requests `<name>` after
 * appending exactly one of png/low, png/med, or png/high. This function makes
 * those two representations equivalent. UI art below png/ui and every other
 * asset family remain byte-for-byte untouched. Unsafe/absolute paths also
 * remain untouched so their normal checked loader can reject them.
 */
[[nodiscard]] std::filesystem::path
texture_quality_asset(std::filesystem::path requested,
                      TextureQualityTier quality);

} // namespace battlespades::render
