#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::frontend {

/**
 * The six Construct Library tabs recovered from constants_prefabs.py.
 *
 * Values intentionally follow the numeric CAT_* order used by SelectPrefabs,
 * because that screen sorts its available category keys before drawing tabs.
 */
enum class UgcPrefabCategory : std::uint8_t {
    landscape = 0U,
    buildings_and_walls,
    nature,
    props,
    road_rail_and_bridges,
    signs_and_banners,
};

/** Return the source-authored category for a retail prefab name. */
[[nodiscard]] std::optional<UgcPrefabCategory>
ugc_prefab_category(std::string_view prefab_name) noexcept;

/** Retail localization key attached to a Construct Library tab. */
[[nodiscard]] std::string_view
ugc_prefab_category_localization_key(UgcPrefabCategory category) noexcept;

/** Exact English display name used by PrefabManager.get_prefab_string(). */
[[nodiscard]] std::optional<std::string_view>
ugc_prefab_display_name(std::string_view prefab_name) noexcept;

/**
 * Retail Size label selected by constants_prefabs.get_prefab_size_category().
 *
 * The generated metadata uses the shipped KV6 voxel count and honours any
 * source-authored size tag before applying the strict <200/<1000/<10000/
 * <50000 thresholds. Unknown server prefabs deliberately return no label.
 */
[[nodiscard]] std::optional<std::string_view>
ugc_prefab_size_localization_key(std::string_view prefab_name) noexcept;

} // namespace battlespades::frontend
