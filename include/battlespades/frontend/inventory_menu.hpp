#pragma once

#include "battlespades/ui/draw_list.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battlespades::frontend {
struct InventoryCosmetic final {
    std::string id, name, kind, rarity, asset, sha256;
    std::array<std::uint8_t, 3U> palette{};
    std::vector<std::string> slots;
    bool owned{}, equipped{};
    std::string author{}, source{}, parent_asset{};
    std::string description{};
    struct WeaponParent {
        std::uint8_t tool{};
        std::string asset;
        float scale{1.0F};
        std::array<float,3U> pivot{};
    };
    std::vector<WeaponParent> parents{};
    std::vector<std::string> crate_versions{};
    std::string scripted_skin{};
    bool enabled{true};
    struct ModelPart {std::string asset,sha256;};
    std::map<std::string,ModelPart> character_parts{};
    std::string character_format{};
    std::string replacement_id{};
    std::map<std::string,std::string> crate_rarities{};
};
struct InventoryCrate final {
    std::string id, level, commitment;
    std::string catalog_version{"supply-v1"};
};
struct InventoryOpening final {
    std::string id, name, rarity, date, commitment, seed;
    bool verified{};
    std::string item_id{};
};
struct InventoryData final {
    std::string level{"1"}, xp{"0"}, level_xp{"0"}, next_xp{"1000"}, revision{"0"},
        crate_count{"0"};
    std::array<std::uint32_t, 3U> pity{};
    std::map<std::string,std::array<std::uint32_t,3U>> pity_by_family{};
    std::vector<InventoryCosmetic> items;
    std::vector<InventoryCrate> crates;
    std::vector<InventoryOpening> history;
    std::vector<std::pair<std::string, std::string>> equipped;
    std::string next_crates, next_history;
    std::vector<std::string> crate_cursors{std::string{}}, history_cursors{std::string{}};
    bool guest{}, opening_enabled{}, equip_enabled{}, awards_enabled{};
};
enum class InventorySection : std::uint8_t { collection, crates, history, packs };
enum class InventoryAction : std::uint8_t {
    none,
    refresh,
    open,
    equip,
    unequip,
    creators,
    more_crates,
    more_history,
    previous_crates,
    previous_history,
    equip_weapon,
    unequip_weapon
};

/** Selection and presentation only. Network ownership and rolls remain authoritative. */
class InventoryMenuModel final {
public:
    InventoryData data;
    InventorySection section{InventorySection::collection};
    std::size_t kind_filter{}, rarity_filter{}, selected{}, page{}, slot_index{};
    bool owned_only{}, busy{}, online{}, loaded{}, preview_ready{}, blue_team{};
    double angle{0.65}, zoom{1.0};
    std::string error;
    std::optional<InventoryOpening> reveal;
    void complete(InventoryData value);
    void fail(std::string message);
    [[nodiscard]] std::vector<std::size_t> filtered_items() const;
    [[nodiscard]] const InventoryCosmetic* selected_item() const;
    [[nodiscard]] const InventoryCrate* selected_crate() const;
    [[nodiscard]] const InventoryCosmetic* equipped_item(std::string_view slot) const;
    [[nodiscard]] InventoryAction click(ui::Point point);
    void move_selection(int delta);
    [[nodiscard]] ui::DrawList build() const;
};
inline constexpr std::string_view inventory_preview_asset{"runtime/inventory-preview"};
[[nodiscard]] InventoryData inventory_crate_pool(const InventoryData& data, std::string_view version);
[[nodiscard]] std::array<double, 5U> inventory_effective_odds(const InventoryData& data,
                                                           std::string_view version = {});
} // namespace battlespades::frontend
