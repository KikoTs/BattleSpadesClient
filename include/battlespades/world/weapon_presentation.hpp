#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::world {
/** Source KV6 coordinates: X across, Y along barrel, Z down. */
struct WeaponSightTags final {
    std::array<float, 3U> rear{};
    std::array<float, 3U> front{};
    float eye_distance{0.8F};
};
struct WeaponPresentation final {
    std::string id;
    std::string model_sha256;
    std::optional<WeaponSightTags> sight;
    std::vector<std::string> fire_samples;
    float fire_gain{1.0F};
    std::map<std::string,std::vector<std::string>> cues{};
};
/** Invalid records fail independently; absent content preserves retail. */
[[nodiscard]] std::vector<WeaponPresentation> load_weapon_presentations(
    const std::filesystem::path& asset_root);
[[nodiscard]] std::optional<WeaponPresentation> weapon_presentation(
    const std::filesystem::path& asset_root, std::string_view id);
/** A tagged point in eye space, before the retail 180-degree yaw. */
[[nodiscard]] std::array<float, 3U> sight_tag_position(
    const WeaponSightTags& tags, std::array<float, 3U> source, float scale) noexcept;
} // namespace battlespades::world
