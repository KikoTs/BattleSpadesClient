#include "battlespades/world/class_selection.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>

namespace battlespades::world {
namespace {

using namespace std::string_view_literals;

using Names = std::span<const std::string_view>;
constexpr std::array soldier{"prefab_ultrabarrier"sv, "prefab_superbarrier"sv,
                             "prefab_supersmallwall"sv, "prefab_fort_wall"sv};
constexpr std::array scout{"prefab_supertower"sv, "prefab_superbridge"sv,
                           "prefab_superminibunker"sv, "prefab_caltrop"sv};
constexpr std::array rocketeer{"prefab_caltrop"sv, "prefab_superminibunker"sv,
                               "prefab_safety_tube"sv};
constexpr std::array miner{"prefab_superdome"sv, "prefab_superpole"sv,
                           "prefab_safety_corridor"sv};
constexpr std::array zombie{"prefab_zombiehand"sv, "prefab_zombiebone"sv,
                            "prefab_zombiehead"sv};
constexpr std::array gangster{"prefab_small_platform"sv, "prefab_ladder"sv,
                              "prefab_square_bunker"sv};
constexpr std::array engineer{
    "prefab_caltrop"sv, "prefab_supertower"sv, "prefab_ultrabarrier"sv,
    "prefab_platform"sv, "prefab_superminibunker"sv, "prefab_superdome"sv,
    "prefab_fort_wall"sv, "prefab_superbridge"sv, "prefab_superpole"sv};
constexpr std::array specialist{"prefab_caltrop"sv, "prefab_superpole"sv,
                                "prefab_fort_wall"sv,
                                "prefab_safety_corridor"sv};
constexpr std::array medic{"prefab_supersmallwall"sv, "prefab_ultrabarrier"sv,
                           "prefab_fort_wall"sv, "prefab_superbridge"sv};
constexpr std::array<std::string_view, 0U> none{};

void append_unique(std::vector<std::uint8_t>& output, std::uint16_t raw) {
    if (raw > 255U) return;
    const auto value = static_cast<std::uint8_t>(raw);
    // SetClassLoadout is a byte-sized equipment list, not just ClientData's
    // held-tool field. Preserve recovered jetpack/glider ids above 64; the
    // PlayerInventory façade filters them out of selectable weapon slots.
    if ((raw <= 64U && !valid_selectable_tool(value)) ||
        std::ranges::find(output, value) != output.end()) {
        return;
    }
    output.push_back(value);
}

} // namespace

std::span<const std::string_view>
class_prefab_options(std::uint8_t class_id) noexcept {
    switch (class_id) {
    case 0U: return soldier;
    case 1U: return scout;
    case 2U: return rocketeer;
    case 3U: return miner;
    case 4U:
    case 14U:
    case 15U: return zombie;
    case 6U:
    case 7U:
    case 8U:
    case 9U:
    case 10U:
    case 11U: return gangster;
    case 12U: return engineer;
    case 16U: return specialist;
    case 17U: return medic;
    default: return none;
    }
}

ClassSelection make_class_selection(
    std::uint8_t class_id,
    const std::array<std::size_t, 4U>& option_indices,
    std::span<const std::string> prefabs) {
    ClassSelection selection;
    selection.class_id = class_id;
    const auto* definition = find_class_definition(class_id);
    if (definition == nullptr) return selection;

    // CLASS_ITEMS rows are melee, primary, secondary, equipment. Retail keeps
    // one choice per non-empty row, even when the chosen value is a jetpack.
    for (std::size_t group{}; group < option_indices.size(); ++group) {
        const auto options = definition->item_groups[group];
        if (!options.empty()) {
            append_unique(selection.loadout,
                          options[option_indices[group] % options.size()]);
        }
    }
    const auto common = definition->item_groups[
        static_cast<std::size_t>(ClassItemGroup::common)];
    // BLOCK_TOOL is inserted at the beginning by GameClass. Tool 22 is the
    // optional flare block and must never be confused with prefab tool 23.
    for (const auto item : common) {
        if (item == 5U) {
            selection.loadout.insert(selection.loadout.begin(), 5U);
        } else if (item != 22U) {
            append_unique(selection.loadout, item);
        }
    }
    // Normal non-mafia classes expose the prefab builder even if a recovered
    // common row omitted it. De-duplication preserves its retail position.
    append_unique(selection.loadout, 23U);

    const auto allowed = class_prefab_options(class_id);
    for (const auto& requested : prefabs) {
        if (selection.prefabs.size() == 3U) break;
        const auto found = std::ranges::find(allowed, requested);
        if (found != allowed.end() &&
            std::ranges::find(selection.prefabs, requested) ==
                selection.prefabs.end()) {
            selection.prefabs.push_back(requested);
        }
    }
    for (const auto value : allowed) {
        if (selection.prefabs.size() == 3U) break;
        const std::string name{value};
        if (std::ranges::find(selection.prefabs, name) == selection.prefabs.end()) {
            selection.prefabs.push_back(name);
        }
    }
    return selection;
}

ClassSelection default_class_selection(std::uint8_t class_id) {
    return make_class_selection(class_id, {}, {});
}

} // namespace battlespades::world
