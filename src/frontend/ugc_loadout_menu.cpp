#include "battlespades/frontend/ugc_loadout_menu.hpp"

#include "battlespades/world/entity_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ranges>
#include <string_view>

namespace battlespades::frontend {
namespace {

constexpr std::array<UgcPrefabCategory, 6U> prefab_categories{{
    UgcPrefabCategory::landscape,
    UgcPrefabCategory::buildings_and_walls,
    UgcPrefabCategory::nature,
    UgcPrefabCategory::props,
    UgcPrefabCategory::road_rail_and_bridges,
    UgcPrefabCategory::signs_and_banners,
}};

struct GameDataCategory final {
    std::string_view label;
    std::span<const std::uint8_t> ids;
};

struct ObjectiveRule final {
    std::string_view id;
    std::int32_t minimum{};
    std::int32_t maximum{};
    std::uint8_t priority{};
};

constexpr std::array<ObjectiveRule, 12U> objective_rules{{
    {"UGC_OBJECTIVE_TEAM1_SPAWN_POINTS", 1, 10, 1U},
    {"UGC_OBJECTIVE_TEAM2_SPAWN_POINTS", 1, 10, 1U},
    {"UGC_OBJECTIVE_TEAM1_ZONE", 1, 1, 2U},
    {"UGC_OBJECTIVE_TEAM2_ZONE", 1, 1, 2U},
    {"UGC_OBJECTIVE_TC_NEUTRAL_ZONES", 2, 10, 3U},
    {"UGC_OBJECTIVE_MH_NEUTRAL_ZONES", 2, 10, 3U},
    {"UGC_OBJECTIVE_DIA_NEUTRAL_ZONES", 2, 5, 3U},
    {"UGC_OBJECTIVE_AMMOCRATE_SPAWNS", 2, 25, 4U},
    {"UGC_OBJECTIVE_HEALTHCRATE_SPAWNS", 2, 25, 4U},
    {"UGC_OBJECTIVE_BLOCKCRATE_SPAWNS", 2, 25, 4U},
    {"UGC_OBJECTIVE_BLOCKCOUNT", 0, 100000, 5U},
    {"UGC_OBJECTIVE_BOMB_SPAWNS", 1, 5, 3U},
}};

constexpr std::array<std::uint8_t, 6U> spawn_ids{{4U, 5U, 6U, 7U, 8U, 9U}};
constexpr std::array<std::uint8_t, 10U> base_ids{{
    10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 3U,
}};
constexpr std::array<std::uint8_t, 3U> crate_ids{{0U, 1U, 2U}};
constexpr std::array<GameDataCategory, 3U> game_data_categories{{
    {"UGC_TOOL_CAT_SPAWNS", spawn_ids},
    {"UGC_TOOL_CAT_BASES", base_ids},
    {"UGC_TOOL_CAT_CRATE_DROPS", crate_ids},
}};

constexpr std::array<std::uint8_t, 3U> green_spawn_ids{{4U, 5U, 6U}};
constexpr std::array<std::uint8_t, 3U> blue_spawn_ids{{7U, 8U, 9U}};
constexpr std::array<std::uint8_t, 3U> green_base_ids{{10U, 11U, 12U}};
constexpr std::array<std::uint8_t, 3U> blue_base_ids{{13U, 14U, 15U}};
constexpr std::array<std::uint8_t, 3U> neutral_base_ids{{16U, 17U, 18U}};
constexpr std::array<std::uint8_t, 1U> health_drop_ids{{0U}};
constexpr std::array<std::uint8_t, 1U> ammo_drop_ids{{1U}};
constexpr std::array<std::uint8_t, 1U> block_drop_ids{{2U}};
constexpr std::array<std::uint8_t, 1U> bomb_drop_ids{{3U}};
constexpr std::array<std::uint8_t, 0U> no_entity_ids{};

[[nodiscard]] bool natural_less(std::string_view left,
                                std::string_view right) noexcept {
    std::size_t left_at{};
    std::size_t right_at{};
    while (left_at < left.size() && right_at < right.size()) {
        const auto left_byte = static_cast<unsigned char>(left[left_at]);
        const auto right_byte = static_cast<unsigned char>(right[right_at]);
        if (std::isdigit(left_byte) != 0 && std::isdigit(right_byte) != 0) {
            std::size_t left_end{left_at};
            std::size_t right_end{right_at};
            while (left_end < left.size() &&
                   std::isdigit(static_cast<unsigned char>(left[left_end])) != 0) {
                ++left_end;
            }
            while (right_end < right.size() &&
                   std::isdigit(static_cast<unsigned char>(right[right_end])) != 0) {
                ++right_end;
            }
            auto left_significant = left_at;
            auto right_significant = right_at;
            while (left_significant + 1U < left_end && left[left_significant] == '0') {
                ++left_significant;
            }
            while (right_significant + 1U < right_end && right[right_significant] == '0') {
                ++right_significant;
            }
            const auto left_digits = left_end - left_significant;
            const auto right_digits = right_end - right_significant;
            if (left_digits != right_digits) return left_digits < right_digits;
            const auto digit_compare = left.substr(left_significant, left_digits).compare(
                right.substr(right_significant, right_digits));
            if (digit_compare != 0) return digit_compare < 0;
            left_at = left_end;
            right_at = right_end;
            continue;
        }
        const auto folded_left = static_cast<unsigned char>(std::tolower(left_byte));
        const auto folded_right = static_cast<unsigned char>(std::tolower(right_byte));
        if (folded_left != folded_right) return folded_left < folded_right;
        ++left_at;
        ++right_at;
    }
    return left.size() < right.size();
}

[[nodiscard]] std::string prefab_label_key(std::string_view name) {
    std::string key{name};
    std::ranges::transform(key, key.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return key;
}

[[nodiscard]] std::string prefab_preview_asset(std::string_view name) {
    std::string lowered{name};
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return "ugc/prefabs/" + lowered + ".png";
}

[[nodiscard]] bool contains(std::span<const std::uint8_t> values,
                            std::uint8_t value) noexcept {
    return std::ranges::find(values, value) != values.end();
}

} // namespace

std::span<const std::uint8_t>
ugc_loadout_entity_ids_for_objective(std::string_view objective_id) noexcept {
    // shared/constants_ugc_objectives.py:UGC_OBJECTIVES_TYPES. Team numbering
    // is intentionally not inferred: TEAM1 is blue ids 7..9/13..15 while
    // TEAM2 is green ids 4..6/10..12 in the retail constant table.
    if (objective_id == "UGC_OBJECTIVE_TEAM1_SPAWN_POINTS") return blue_spawn_ids;
    if (objective_id == "UGC_OBJECTIVE_TEAM2_SPAWN_POINTS") return green_spawn_ids;
    if (objective_id == "UGC_OBJECTIVE_TEAM1_ZONE") return blue_base_ids;
    if (objective_id == "UGC_OBJECTIVE_TEAM2_ZONE") return green_base_ids;
    if (objective_id == "UGC_OBJECTIVE_TC_NEUTRAL_ZONES" ||
        objective_id == "UGC_OBJECTIVE_MH_NEUTRAL_ZONES" ||
        objective_id == "UGC_OBJECTIVE_DIA_NEUTRAL_ZONES") {
        return neutral_base_ids;
    }
    if (objective_id == "UGC_OBJECTIVE_HEALTHCRATE_SPAWNS") return health_drop_ids;
    if (objective_id == "UGC_OBJECTIVE_AMMOCRATE_SPAWNS") return ammo_drop_ids;
    if (objective_id == "UGC_OBJECTIVE_BLOCKCRATE_SPAWNS") return block_drop_ids;
    if (objective_id == "UGC_OBJECTIVE_BOMB_SPAWNS") return bomb_drop_ids;
    // BLOCKCOUNT has no entity_ids in the source table. Unknown future ids
    // also fail closed instead of unlocking every editor marker.
    return no_entity_ids;
}

std::optional<UgcLoadoutObjective>
ugc_loadout_objective(std::string_view objective_id,
                      std::int32_t value) noexcept {
    const auto rule = std::ranges::find(objective_rules, objective_id,
                                        &ObjectiveRule::id);
    if (rule == objective_rules.end()) return std::nullopt;
    return UgcLoadoutObjective{std::string{objective_id}, value,
                               rule->minimum, rule->maximum, rule->priority};
}

void UgcLoadoutMenuModel::configure(
    UgcLoadoutLibrary library,
    std::span<const std::string> server_prefabs,
    std::span<const std::uint8_t> allowed_ugc_tools,
    std::span<const std::string> selected_prefabs,
    std::span<const std::uint8_t> selected_ugc_tools,
    std::uint8_t team,
    bool in_game,
    std::span<const UgcLoadoutObjective> objectives) {
    library_ = library;
    team_ = team;
    in_game_ = in_game;
    construct_tab_index_ = 0U;
    game_data_tab_index_ = 0U;
    hovered_.reset();
    pending_audio_cue_.reset();
    build_construct_tabs(server_prefabs);
    build_game_data_tabs(allowed_ugc_tools);
    rebuild_inventory(selected_prefabs, selected_ugc_tools);
    set_objectives(objectives);
}

void UgcLoadoutMenuModel::open_library(UgcLoadoutLibrary library) noexcept {
    library_ = library;
    hovered_.reset();
    pending_audio_cue_.reset();
}

std::span<const UgcLoadoutTab> UgcLoadoutMenuModel::tabs() const noexcept {
    return active_tabs();
}

std::size_t UgcLoadoutMenuModel::current_tab_index() const noexcept {
    const auto& values = active_tabs();
    if (values.empty()) return 0U;
    return std::min(library_ == UgcLoadoutLibrary::constructs
                        ? construct_tab_index_
                        : game_data_tab_index_,
                    values.size() - 1U);
}

const UgcLoadoutTab* UgcLoadoutMenuModel::current_tab() const noexcept {
    const auto& values = active_tabs();
    return values.empty() ? nullptr : &values[current_tab_index()];
}

std::span<const UgcLoadoutItem>
UgcLoadoutMenuModel::visible_items() const noexcept {
    const auto* tab = current_tab();
    if (tab == nullptr || tab->first_visible_item >= tab->items.size()) return {};
    const auto count = std::min(items_per_page(),
                                tab->items.size() - tab->first_visible_item);
    return std::span<const UgcLoadoutItem>{tab->items}.subspan(
        tab->first_visible_item, count);
}

std::size_t UgcLoadoutMenuModel::columns() const noexcept {
    return library_ == UgcLoadoutLibrary::constructs ? 7U : 3U;
}

std::size_t UgcLoadoutMenuModel::items_per_page() const noexcept {
    return library_ == UgcLoadoutLibrary::constructs ? 14U : 6U;
}

std::span<const UgcLoadoutChoice>
UgcLoadoutMenuModel::inventory() const noexcept {
    return inventory_;
}

const UgcLoadoutItem*
UgcLoadoutMenuModel::item(const UgcLoadoutChoice& choice) const noexcept {
    const auto find_in = [&](const std::vector<UgcLoadoutTab>& values)
        -> const UgcLoadoutItem* {
        for (const auto& tab : values) {
            const auto found = std::ranges::find_if(
                tab.items, [&](const UgcLoadoutItem& candidate) {
                    return candidate.choice == choice;
                });
            if (found != tab.items.end()) return &*found;
        }
        return nullptr;
    };
    return choice.kind == UgcLoadoutItemKind::prefab
               ? find_in(construct_tabs_)
               : find_in(game_data_tabs_);
}

bool UgcLoadoutMenuModel::selected(const UgcLoadoutChoice& choice) const noexcept {
    return std::ranges::find(inventory_, choice) != inventory_.end();
}

world::ClassSelection UgcLoadoutMenuModel::selection() const {
    world::ClassSelection result;
    result.class_id = 13U; // CLASS_UGCBUILDER
    // create_loadout_list() followed by GameClass.set_common_loadout_items().
    // The duplicate INTEL_TOOL byte is intentional and source-authored.
    result.loadout = {5U, 45U, 47U, 48U, 69U, 43U,
                      30U, 42U, 41U, 25U, 26U, 30U};
    for (const auto& choice : inventory_) {
        if (choice.kind == UgcLoadoutItemKind::prefab) {
            if (std::ranges::find(result.prefabs, choice.prefab_name) ==
                result.prefabs.end()) {
                result.prefabs.push_back(choice.prefab_name);
            }
        } else if (std::ranges::find(result.ugc_tools, choice.ugc_tool) ==
                   result.ugc_tools.end()) {
            result.ugc_tools.push_back(choice.ugc_tool);
        }
    }
    return result;
}

std::optional<UgcLoadoutAudioCue>
UgcLoadoutMenuModel::take_audio_cue() noexcept {
    const auto result = pending_audio_cue_;
    pending_audio_cue_.reset();
    return result;
}

void UgcLoadoutMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point;
}

std::optional<UgcLoadoutAction>
UgcLoadoutMenuModel::click(ui::Point point) {
    if (select_bounds().contains(point)) {
        pending_audio_cue_ = UgcLoadoutAudioCue::confirm;
        return UgcLoadoutAction::submit;
    }
    if (back_bounds(in_game_).contains(point)) {
        pending_audio_cue_ = UgcLoadoutAudioCue::back;
        return UgcLoadoutAction::back;
    }
    for (std::size_t index{}; index < maximum_selection; ++index) {
        if (inventory_bounds(index).contains(point) && index < inventory_.size()) {
            remove_inventory(index);
            pending_audio_cue_ = UgcLoadoutAudioCue::scroll;
            return std::nullopt;
        }
    }
    for (std::size_t index{}; index < tabs().size(); ++index) {
        if (tab_bounds(index).contains(point)) {
            select_tab(index);
            pending_audio_cue_ = UgcLoadoutAudioCue::scroll;
            return std::nullopt;
        }
    }
    const auto visible = visible_items();
    for (std::size_t index{}; index < visible.size(); ++index) {
        if (visible_item_bounds(index).contains(point)) {
            select_item(visible[index].choice);
            pending_audio_cue_ = UgcLoadoutAudioCue::scroll;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void UgcLoadoutMenuModel::select_tab(std::size_t index) noexcept {
    const auto& values = active_tabs();
    if (values.empty()) return;
    const auto clamped = std::min(index, values.size() - 1U);
    if (library_ == UgcLoadoutLibrary::constructs) {
        construct_tab_index_ = clamped;
    } else {
        game_data_tab_index_ = clamped;
    }
}

void UgcLoadoutMenuModel::scroll_rows(int direction) noexcept {
    auto& values = active_tabs();
    if (values.empty() || direction == 0) return;
    auto& tab = values[current_tab_index()];
    const auto column_count = columns();
    const auto visible_rows = items_per_page() / column_count;
    // GridSelection receives int(item_count / columns) + 1. Preserve the
    // original extra final row when a category exactly fills a page.
    const auto total_rows = tab.items.size() / column_count + 1U;
    const auto max_row = total_rows > visible_rows ? total_rows - visible_rows : 0U;
    auto row = tab.first_visible_item / column_count;
    if (direction > 0) {
        row = std::min(max_row, row + 1U);
    } else if (row > 0U) {
        --row;
    }
    tab.first_visible_item = row * column_count;
    pending_audio_cue_ = UgcLoadoutAudioCue::scroll;
}

void UgcLoadoutMenuModel::select_item(const UgcLoadoutChoice& choice) {
    const auto found = std::ranges::find(inventory_, choice);
    if (found != inventory_.end()) {
        inventory_.erase(found);
        return;
    }
    if (!available(choice)) return;
    if (inventory_.size() >= maximum_selection) inventory_.erase(inventory_.begin());
    inventory_.push_back(choice);
}

void UgcLoadoutMenuModel::remove_inventory(std::size_t index) noexcept {
    if (index < inventory_.size()) {
        inventory_.erase(inventory_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void UgcLoadoutMenuModel::set_objectives(
    std::span<const UgcLoadoutObjective> objectives) {
    objectives_.clear();
    objectives_.reserve(objectives.size());
    for (const auto& objective : objectives) {
        if (std::ranges::find_if(objectives_, [&](const auto& candidate) {
                return candidate.id == objective.id;
            }) == objectives_.end()) {
            objectives_.push_back(objective);
        }
    }
    // Python sorted(..., key=priority) is stable. Equal-priority rows retain
    // the packet/dictionary traversal order instead of gaining a native-only
    // alphabetical order.
    std::ranges::stable_sort(objectives_, {}, &UgcLoadoutObjective::priority);
}

ui::Rect UgcLoadoutMenuModel::select_bounds() noexcept {
    // TextButton(422, 163, 310, 60): y is the bottom-origin top edge.
    return {422, 437, 310, 60};
}

ui::Rect UgcLoadoutMenuModel::back_bounds(bool in_game) noexcept {
    return in_game ? ui::Rect{338, 521, 125, 45}
                   : ui::Rect{54, 541, 130, 35};
}

ui::Rect UgcLoadoutMenuModel::inventory_bounds(std::size_t index) noexcept {
    // first_x 98, pad_x 23.5, width 32; integer bounds preserve half-pixel
    // centres without creating overlapping hit regions.
    const auto left = static_cast<std::int32_t>(
        std::ceil(137.5 + 55.5 * static_cast<double>(index)));
    return {left, 462, 32, 32};
}

ui::Rect UgcLoadoutMenuModel::tab_bounds(std::size_t index) const noexcept {
    const auto selected_index = current_tab_index();
    const bool selected_tab = index == selected_index;
    const auto width = library_ == UgcLoadoutLibrary::constructs ? 110 : 114;
    const auto height = selected_tab ? 37 : 32;
    const auto center_x = 56.0 + (static_cast<double>(index) + 1.0) * 4.25 +
                          static_cast<double>(index * static_cast<std::size_t>(width)) +
                          static_cast<double>(width) * 0.5;
    const auto center_y = selected_tab ? 485.0 : 488.0;
    return {static_cast<std::int32_t>(center_x - static_cast<double>(width) * 0.5),
            static_cast<std::int32_t>(600.0 - center_y -
                                      static_cast<double>(height) * 0.5),
            width, height};
}

ui::Rect UgcLoadoutMenuModel::visible_item_bounds(std::size_t index) const noexcept {
    const auto column = index % columns();
    const auto row = index / columns();
    if (library_ == UgcLoadoutLibrary::constructs) {
        return {67 + static_cast<std::int32_t>(column * 93U),
                139 + static_cast<std::int32_t>(row * 140U), 86, 134};
    }
    return {67 + static_cast<std::int32_t>(column * 114U),
            140 + static_cast<std::int32_t>(row * 139U), 107, 133};
}

std::vector<UgcLoadoutTab>& UgcLoadoutMenuModel::active_tabs() noexcept {
    return library_ == UgcLoadoutLibrary::constructs ? construct_tabs_
                                                      : game_data_tabs_;
}

const std::vector<UgcLoadoutTab>&
UgcLoadoutMenuModel::active_tabs() const noexcept {
    return library_ == UgcLoadoutLibrary::constructs ? construct_tabs_
                                                      : game_data_tabs_;
}

void UgcLoadoutMenuModel::build_construct_tabs(
    std::span<const std::string> server_prefabs) {
    construct_tabs_.clear();
    for (const auto category : prefab_categories) {
        UgcLoadoutTab tab;
        tab.label_key = std::string{ugc_prefab_category_localization_key(category)};
        for (const auto& name : server_prefabs) {
            if (const auto source_category = ugc_prefab_category(name);
                source_category == category &&
                std::ranges::find_if(tab.items, [&](const UgcLoadoutItem& item) {
                    return item.choice.prefab_name == name;
                }) == tab.items.end()) {
                tab.items.push_back({
                    {UgcLoadoutItemKind::prefab, name, 0U},
                    prefab_label_key(name), prefab_preview_asset(name),
                    std::string{ugc_prefab_size_localization_key(name)
                                    .value_or("UGC_PREFAB_SIZE_LARGEST")}});
            }
        }
        std::ranges::sort(tab.items, [](const UgcLoadoutItem& left,
                                       const UgcLoadoutItem& right) {
            const auto left_name = ugc_prefab_display_name(left.label_key)
                                       .value_or(std::string_view{left.label_key});
            const auto right_name = ugc_prefab_display_name(right.label_key)
                                        .value_or(std::string_view{right.label_key});
            return natural_less(left_name, right_name);
        });
        if (!tab.items.empty()) construct_tabs_.push_back(std::move(tab));
    }
}

void UgcLoadoutMenuModel::build_game_data_tabs(
    std::span<const std::uint8_t> allowed_ugc_tools) {
    game_data_tabs_.clear();
    for (const auto& category : game_data_categories) {
        UgcLoadoutTab tab;
        tab.label_key = std::string{category.label};
        for (const auto id : category.ids) {
            const auto icon = world::ugc_tool_icon_asset(id);
            if (!contains(allowed_ugc_tools, id) || icon.empty()) continue;
            tab.items.push_back({
                {UgcLoadoutItemKind::game_data, {}, id},
                "A" + std::to_string(482U + id), std::string{icon}, {}});
        }
        if (!tab.items.empty()) game_data_tabs_.push_back(std::move(tab));
    }
}

void UgcLoadoutMenuModel::rebuild_inventory(
    std::span<const std::string> selected_prefabs,
    std::span<const std::uint8_t> selected_ugc_tools) {
    inventory_.clear();
    for (const auto& name : selected_prefabs) {
        const UgcLoadoutChoice choice{UgcLoadoutItemKind::prefab, name, 0U};
        if (available(choice) && !selected(choice)) {
            inventory_.push_back(choice);
            if (inventory_.size() == maximum_selection) return;
        }
    }
    for (const auto id : selected_ugc_tools) {
        const UgcLoadoutChoice choice{UgcLoadoutItemKind::game_data, {}, id};
        if (available(choice) && !selected(choice)) {
            inventory_.push_back(choice);
            if (inventory_.size() == maximum_selection) return;
        }
    }
}

bool UgcLoadoutMenuModel::available(const UgcLoadoutChoice& choice) const noexcept {
    const auto has_choice = [&](const std::vector<UgcLoadoutTab>& values) {
        return std::ranges::any_of(values, [&](const UgcLoadoutTab& tab) {
            return std::ranges::any_of(tab.items, [&](const UgcLoadoutItem& item) {
                return item.choice == choice;
            });
        });
    };
    return choice.kind == UgcLoadoutItemKind::prefab
               ? has_choice(construct_tabs_)
               : has_choice(game_data_tabs_);
}

} // namespace battlespades::frontend
