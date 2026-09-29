#include "battlespades/world/class_selection.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>

namespace battlespades::world {
namespace {

constexpr std::uint16_t block_tool{5U};

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

[[nodiscard]] bool flare_tile_class(std::uint8_t class_id) noexcept {
    // selectClass.get_class_images: no flare tile for CLASS_ZOMBIE or
    // CLASS_CLASSIC_SOLDIER. Fast/jump zombies share the zombie picker.
    return class_id != 4U && class_id != 5U && class_id != 14U && class_id != 15U;
}

/** set_common_loadout_items: BLOCK first, the rest appended, 22 only on request. */
void append_common(std::vector<std::uint8_t>& loadout, const ClassDefinition& definition,
                   const ClassSelectionRules& rules, bool add_flareblock) {
    const auto common = definition.item_groups[static_cast<std::size_t>(ClassItemGroup::common)];
    for (const auto item : common) {
        if (rules.tool_disabled(item)) continue;
        if (item == flare_block_tool && !add_flareblock) continue;
        if (item == block_tool) {
            if (std::ranges::find(loadout, static_cast<std::uint8_t>(block_tool)) == loadout.end())
                loadout.insert(loadout.begin(), static_cast<std::uint8_t>(block_tool));
            continue;
        }
        append_unique(loadout, item);
    }
}

} // namespace

bool ClassSelectionRules::tool_disabled(std::uint16_t tool) const noexcept {
    return tool <= 255U &&
           std::ranges::find(disabled_tools, static_cast<std::uint8_t>(tool)) !=
               disabled_tools.end();
}

std::span<const std::string_view>
class_prefab_options(std::uint8_t class_id) noexcept {
    return class_prefab_names(class_id);
}

std::vector<std::uint16_t>
class_row_options(std::uint8_t class_id, std::size_t group, const ClassSelectionRules& rules) {
    std::vector<std::uint16_t> result;
    const auto* definition = find_class_definition(class_id);
    if (definition == nullptr || group >= 4U) return result;
    for (const auto item : definition->item_groups[group]) {
        if (!rules.tool_disabled(item)) result.push_back(item);
    }
    return result;
}

std::vector<std::string>
class_construct_options(std::uint8_t class_id, const ClassSelectionRules& rules) {
    std::vector<std::string> result;
    if (flare_tile_class(class_id) && !rules.tool_disabled(flare_block_tool)) {
        result.emplace_back(flare_block_construct);
    }
    if (rules.tool_disabled(prefab_tool)) return result;
    for (const auto name : class_prefab_options(class_id)) result.emplace_back(name);
    return result;
}

std::vector<std::string>
default_class_constructs(std::uint8_t class_id, const ClassSelectionRules& rules) {
    std::vector<std::string> result;
    if (rules.mafia || rules.ugc || rules.tool_disabled(prefab_tool)) return result;
    const auto available = class_prefab_options(class_id);
    // `while len(prefabs) < n and len(available) >= n`: a class with fewer
    // than three constructs starts with none selected.
    if (available.size() < 3U) return result;
    for (std::size_t index{}; index < 3U; ++index) result.emplace_back(available[index]);
    return result;
}

ClassSelection make_class_selection(std::uint8_t class_id,
                                    std::span<const std::uint16_t> chosen_items,
                                    std::span<const std::string> constructs,
                                    const ClassSelectionRules& rules) {
    ClassSelection selection;
    selection.class_id = class_id;
    const auto* definition = find_class_definition(class_id);
    if (definition == nullptr) return selection;
    for (const auto item : chosen_items) {
        if (!rules.tool_disabled(item)) append_unique(selection.loadout, item);
    }
    const bool flare = std::ranges::find(constructs, std::string{flare_block_construct}) !=
                       constructs.end();
    append_common(selection.loadout, *definition, rules, flare && flare_tile_class(class_id));

    const auto allowed = class_prefab_options(class_id);
    if (!rules.tool_disabled(prefab_tool)) {
        for (const auto& requested : constructs) {
            if (selection.prefabs.size() == 3U) break;
            if (std::ranges::find(allowed, requested) != allowed.end() &&
                std::ranges::find(selection.prefabs, requested) == selection.prefabs.end()) {
                selection.prefabs.push_back(requested);
            }
        }
    }
    return selection;
}

ClassSelection automatic_class_selection(std::uint8_t class_id, const ClassSelectionRules& rules) {
    std::vector<std::uint16_t> chosen;
    for (std::size_t group{}; group < 4U; ++group) {
        const auto options = class_row_options(class_id, group, rules);
        if (!options.empty()) chosen.push_back(options.front());
    }
    const auto constructs = default_class_constructs(class_id, rules);
    auto selection = make_class_selection(class_id, chosen, constructs, rules);
    if (!rules.mafia && find_class_definition(class_id) != nullptr) {
        if (!rules.tool_disabled(flare_block_tool)) append_unique(selection.loadout, flare_block_tool);
        if (!rules.tool_disabled(prefab_tool)) append_unique(selection.loadout, prefab_tool);
    }
    return selection;
}

ClassSelection make_class_selection(
    std::uint8_t class_id,
    const std::array<std::size_t, 4U>& option_indices,
    std::span<const std::string> prefabs) {
    const ClassSelectionRules rules;
    std::vector<std::uint16_t> chosen;
    for (std::size_t group{}; group < option_indices.size(); ++group) {
        const auto options = class_row_options(class_id, group, rules);
        if (!options.empty()) chosen.push_back(options[option_indices[group] % options.size()]);
    }
    return make_class_selection(class_id, chosen, prefabs, rules);
}

ClassSelection default_class_selection(std::uint8_t class_id) {
    std::vector<std::string> prefabs;
    for (const auto value : class_prefab_options(class_id)) {
        if (prefabs.size() == 3U) break;
        prefabs.emplace_back(value);
    }
    return make_class_selection(class_id, {}, prefabs);
}

} // namespace battlespades::world
