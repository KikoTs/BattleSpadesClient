#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::world {

/** Complete retail class transaction carried by SetClassLoadout(13). */
struct ClassSelection final {
    std::uint8_t class_id{};
    std::vector<std::uint8_t> loadout;
    std::vector<std::string> prefabs;
    std::vector<std::uint8_t> ugc_tools;
};

/** Stock prefab names allowed by CLASS_ITEMS/PREFAB_LISTS for one class. */
[[nodiscard]] std::span<const std::string_view>
class_prefab_options(std::uint8_t class_id) noexcept;

/** Build a valid one-choice-per-row selection, including three prefabs. */
[[nodiscard]] ClassSelection default_class_selection(std::uint8_t class_id);

/** Build a selection from four explicit class row indices. */
[[nodiscard]] ClassSelection make_class_selection(
    std::uint8_t class_id,
    const std::array<std::size_t, 4U>& option_indices,
    std::span<const std::string> prefabs);

} // namespace battlespades::world
