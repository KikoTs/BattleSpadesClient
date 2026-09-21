#include "battlespades/frontend/inventory_menu.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace battlespades::frontend {
namespace {
using namespace ui;
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U}, gold{232U, 207U, 78U, 255U};
constexpr std::array<std::string_view, 5U> kinds{
    "ALL TYPES", "weapon_model", "character_skin", "tombstone", "prop_model"};
constexpr std::array<std::string_view, 6U> rarities{
    "ALL RARITIES", "common", "uncommon", "rare", "epic", "legendary"};
constexpr std::array<std::string_view, 5U> kind_labels{
    "ALL TYPES", "WEAPONS", "CHARACTERS", "DEATH MODELS", "WORLD OBJECTS"};
std::string_view crate_name(std::string_view version) {
    if (version.starts_with("weapons-")) return "Weapon Crate";
    if (version.starts_with("characters-")) return "Character Crate";
    if (version.starts_with("cosmetics-")) return "Cosmetic Crate";
    return "Supply Crate";
}
constexpr std::size_t page_size{6U};
ColorRgba8 rarity_color(std::string_view value) {
    if (value == "uncommon")
        return {140U, 178U, 90U, 255U};
    if (value == "rare")
        return {115U, 172U, 211U, 255U};
    if (value == "epic")
        return {183U, 143U, 199U, 255U};
    if (value == "legendary")
        return gold;
    return cream;
}
void image(DrawList& list,
           std::string_view path,
           DrawRect rect,
           ColorRgba8 tint = {255U, 255U, 255U, 255U}) {
    SpriteDrawCommand command;
    command.asset_id = path;
    command.destination = rect;
    command.modulation.color = tint;
    list.push(std::move(command));
}
void fill(DrawList& list, DrawRect rect, ColorRgba8 tint) {
    image(list, "png/high/white.png", rect, tint);
}
void label(DrawList& list,
           std::string_view value,
           DrawRect rect,
           double size = 12.0,
           ColorRgba8 tint = cream,
           bool title = false,
           HorizontalTextAlignment align = HorizontalTextAlignment::left) {
    TextDrawCommand command;
    command.localization_key = value;
    command.destination = rect;
    command.preferred_font_asset = title ? "fonts/Edo.ttf" : "fonts/A750-Sans-Medium.ttf";
    command.requested_font_size_pixels = size;
    command.horizontal_alignment = align;
    command.vertical_alignment = VerticalTextAlignment::center;
    command.fit = TextFit::shrink_to_fit;
    command.modulation.color = tint;
    list.push(std::move(command));
}
void button(DrawList& list, DrawRect rect, std::string_view value, bool enabled = true) {
    const auto cap = rect.height * 37.0 / 60.0;
    const ColorRgba8 tint = enabled ? ColorRgba8{} : ColorRgba8{130U, 130U, 130U, 255U};
    image(list,
          "png/ui/common_elements/buttons/button_large_left.png",
          {rect.x, rect.y, cap, rect.height},
          tint);
    image(list,
          "png/ui/common_elements/buttons/button_large_mid.png",
          {rect.x + cap, rect.y, rect.width - cap * 2.0, rect.height},
          tint);
    image(list,
          "png/ui/common_elements/buttons/button_large_right.png",
          {rect.x + rect.width - cap, rect.y, cap, rect.height},
          tint);
    label(list,
          value,
          rect,
          rect.height > 40.0 ? 20.0 : 11.0,
          {24U, 23U, 18U, 255U},
          true,
          HorizontalTextAlignment::center);
}
bool inside(Point p, DrawRect r) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
std::uint64_t decimal(std::string_view value) {
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? result : 0U;
}
} // namespace

InventoryData inventory_crate_pool(const InventoryData& data, std::string_view version) {
    InventoryData result;
    result.pity=data.pity;
    for (const auto& item : data.items) if (std::ranges::find(item.crate_versions,version)!=item.crate_versions.end()) {
        result.items.push_back(item);
        // Rarity belongs to the frozen reward pool, even when a later release
        // changes that model's presentation tier.
        const auto rarity=item.crate_rarities.find(std::string{version});
        if(rarity!=item.crate_rarities.end())result.items.back().rarity=rarity->second;
    }
    const auto family = std::string{version.substr(0U,version.find('-'))};
    if (family!="supply") {
        const auto found=data.pity_by_family.find(family);
        result.pity=found==data.pity_by_family.end() ? std::array<std::uint32_t,3U>{} : found->second;
    }
    return result;
}

std::array<double, 5U> inventory_effective_odds(const InventoryData& source, std::string_view version) {
    const auto data = version.empty() ? source : inventory_crate_pool(source,version);
    std::array<std::size_t, 5U> counts{};
    for (const auto& item : data.items)
        if (!item.owned) {
            const auto found = std::ranges::find(rarities, item.rarity);
            if (found != rarities.end() && found != rarities.begin())
                ++counts[static_cast<std::size_t>(found - rarities.begin() - 1)];
        }
    std::size_t minimum{};
    constexpr std::array<std::uint32_t, 3U> thresholds{9U, 39U, 99U};
    for (std::size_t offset = 0U; offset < 3U; ++offset) {
        const auto counter = 2U - offset, candidate = 4U - offset;
        if (data.pity[counter] < thresholds[counter])
            continue;
        bool available{};
        for (std::size_t i = candidate; i < 5U; ++i)
            available |= counts[i] > 0U;
        if (available) {
            minimum = candidate;
            break;
        }
    }
    constexpr std::array<double, 5U> weights{55.0, 27.0, 12.0, 5.0, 1.0};
    std::array<double, 5U> odds{};
    double total{};
    for (std::size_t i = 0U; i < 5U; ++i)
        if (counts[i] && i >= minimum)
            total += (odds[i] = weights[i]);
    if (total > 0.0)
        for (auto& value : odds)
            value /= total;
    return odds;
}

void InventoryMenuModel::complete(InventoryData value) {
    const auto old = section==InventorySection::collection && selected_item() ? selected_item()->id :
        section==InventorySection::crates && selected_crate() ? selected_crate()->id :
        section==InventorySection::history && selected<data.history.size()?data.history[selected].id:std::string{};
    data = std::move(value);
    busy = false;
    online = true;
    loaded = true;
    error.clear();
    const auto items = filtered_items();
    const auto count=section==InventorySection::collection?items.size():section==InventorySection::crates?data.crates.size():data.history.size();
    selected=count?std::min(selected,count-1U):0U;
    for (std::size_t i = 0U; i < count; ++i) {
        const auto& id=section==InventorySection::collection?data.items[items[i]].id:section==InventorySection::crates?data.crates[i].id:data.history[i].id;
        if(id==old)selected=i;
    }
    page = selected / page_size;
}
void InventoryMenuModel::fail(std::string message) {
    busy = false;
    online = false;
    error = std::move(message);
}
std::vector<std::size_t> InventoryMenuModel::filtered_items() const {
    std::vector<std::size_t> result;
    for (std::size_t i = 0U; i < data.items.size(); ++i) {
        const auto& item = data.items[i];
        if (item.enabled && (!owned_only || item.owned) && (kind_filter == 0U || item.kind == kinds[kind_filter]) &&
            (rarity_filter == 0U || item.rarity == rarities[rarity_filter]))
            result.push_back(i);
    }
    return result;
}
const InventoryCosmetic* InventoryMenuModel::selected_item() const {
    const auto items = filtered_items();
    return selected < items.size() ? &data.items[items[selected]] : nullptr;
}
const InventoryCrate* InventoryMenuModel::selected_crate() const {
    return selected < data.crates.size() ? &data.crates[selected] : nullptr;
}
const InventoryCosmetic* InventoryMenuModel::equipped_item(std::string_view slot) const {
    for (const auto& pair : data.equipped)
        if (pair.first == slot)
            for (const auto& item : data.items)
                if (item.id == pair.second && item.owned && item.enabled)
                    return &item;
    return nullptr;
}
void InventoryMenuModel::move_selection(int delta) {
    const auto count = section == InventorySection::collection ? filtered_items().size()
                       : section == InventorySection::crates   ? data.crates.size()
                                                               : data.history.size();
    if (count == 0U)
        return;
    selected = static_cast<std::size_t>(
        std::clamp(static_cast<int>(selected) + delta, 0, static_cast<int>(count - 1U)));
    page = selected / page_size;
    slot_index = 0U;
}
InventoryAction InventoryMenuModel::click(Point point) {
    if (reveal) {
        if (inside(point, {515, 500, 225, 48}))
            reveal.reset();
        return InventoryAction::none;
    }
    if (inside(point, {245, 500, 145, 48}) && !busy)
        return InventoryAction::refresh;
    if (inside(point, {60, 185, 680, 30})) {
        section = static_cast<InventorySection>(std::clamp((point.x - 60) / 170, 0, 3));
        page = selected = slot_index = 0U;
        return InventoryAction::none;
    }
    if (section == InventorySection::packs) {
        return inside(point, {515, 500, 225, 48}) ? InventoryAction::creators
                                                  : InventoryAction::none;
    }
    if (inside(point, {60, 445, 70, 28})) {
        if (page == 0U && !busy) {
            if (section == InventorySection::crates && data.crate_cursors.size() > 1U)
                return InventoryAction::previous_crates;
            if (section == InventorySection::history && data.history_cursors.size() > 1U)
                return InventoryAction::previous_history;
        }
        move_selection(-static_cast<int>(page_size));
        return InventoryAction::none;
    }
    if (inside(point, {326, 445, 70, 28})) {
        const auto count = section == InventorySection::collection ? filtered_items().size()
                           : section == InventorySection::crates   ? data.crates.size()
                                                                   : data.history.size();
        if ((page + 1U) * page_size >= count && !busy) {
            if (section == InventorySection::crates && !data.next_crates.empty())
                return InventoryAction::more_crates;
            if (section == InventorySection::history && !data.next_history.empty())
                return InventoryAction::more_history;
        }
        move_selection(static_cast<int>(page_size));
        return InventoryAction::none;
    }
    if (section == InventorySection::collection) {
        if (inside(point, {60, 219, 123, 28})) {
            kind_filter = (kind_filter + 1U) % kinds.size();
            selected = page = 0U;
        } else if (inside(point, {188, 219, 123, 28})) {
            rarity_filter = (rarity_filter + 1U) % rarities.size();
            selected = page = 0U;
        } else if (inside(point, {316, 219, 80, 28})) {
            owned_only = !owned_only;
            selected = page = 0U;
        } else if (inside(point, {435, 390, 55, 28}))
            angle -= 0.4;
        else if (inside(point, {495, 390, 55, 28}))
            angle += 0.4;
        else if (inside(point, {555, 390, 75, 28}))
            blue_team = !blue_team;
        else if (inside(point, {635, 390, 80, 28}))
            zoom = zoom > 1.2 ? 0.8 : zoom + 0.25;
        else if (inside(point, {435, 425, 280, 28})) {
            if (const auto* item = selected_item(); item && !item->slots.empty())
                slot_index = (slot_index + 1U) % item->slots.size();
        } else if (inside(point, {515, 500, 225, 48}) && !busy && online && data.equip_enabled) {
            if (const auto* item = selected_item(); item && item->owned && !item->slots.empty())
                return equipped_item(item->slots[std::min(slot_index, item->slots.size() - 1U)]) ==
                               item
                           ? InventoryAction::unequip
                           : InventoryAction::equip;
        }
    } else if (section == InventorySection::crates && inside(point, {515, 500, 225, 48}) && !busy &&
               online && data.opening_enabled && selected_crate())
        return InventoryAction::open;
    if (inside(point, {60, 251, 336, 186})) {
        const auto row = page * page_size + static_cast<std::size_t>((point.y - 251) / 31);
        const auto count = section == InventorySection::collection ? filtered_items().size()
                           : section == InventorySection::crates   ? data.crates.size()
                                                                   : data.history.size();
        if (row < count) {
            selected = row;
            slot_index = 0U;
        }
    }
    return InventoryAction::none;
}

DrawList InventoryMenuModel::build() const {
    DrawList list;
    fill(list, {54, 139, 692, 343}, {34U, 32U, 28U, 255U});
    label(list, "YOUR ACCOUNT LEVEL " + data.level, {64, 142, 285, 27}, 23.0, gold, true);
    label(list,
          data.crate_count + " SUPPLY CRATES",
          {484, 142, 225, 25},
          17.0,
          cream,
          true,
          HorizontalTextAlignment::right);
    if (const auto* badge = equipped_item("profile_badge"))
        image(list,
              badge->asset,
              {717, 145, 22, 22},
              {badge->palette[0], badge->palette[1], badge->palette[2], 255U});
    const auto next = decimal(data.next_xp), current = decimal(data.level_xp);
    fill(list, {65, 171, 670, 7}, {86U, 100U, 21U, 255U});
    const auto progress =
        next ? static_cast<double>(std::min(current, next)) / static_cast<double>(next) : 0.0;
    // Fresh accounts and exact level-ups have no fill; zero-area sprites are
    // rejected by the native renderer.
    if (progress > 0.0)
        fill(list, {65, 171, 670 * progress, 7}, {137U, 179U, 45U, 255U});
    label(list,
          data.level_xp + " / " + data.next_xp + " XP",
          {310, 145, 175, 20},
          11.0,
          cream,
          false,
          HorizontalTextAlignment::center);
    constexpr std::array<std::string_view, 4U> sections{
        "COLLECTION", "SUPPLY CRATES", "HISTORY", "SKIN PACKS"};
    for (std::size_t i = 0U; i < sections.size(); ++i) {
        fill(list,
             {60 + 170.0 * i, 185, 168, 30},
             i == static_cast<std::size_t>(section) ? ColorRgba8{162U, 58U, 30U, 255U}
                                                    : ColorRgba8{57U, 53U, 44U, 255U});
        label(list,
              sections[i],
              {60 + 170.0 * i, 185, 168, 30},
              17.0,
              cream,
              true,
              HorizontalTextAlignment::center);
    }
    button(list, {60, 500, 170, 48}, "BACK");
    button(list, {245, 500, 145, 48}, busy ? "LOADING" : "REFRESH", !busy);
    std::string status = error;
    if (status.empty())
        status = data.guest ? "Guest collection: register this account to keep a recovery method."
                            : "One free crate per level. Free opening. Cosmetics only.";
    label(list,
          status,
          {60, 474, 680, 22},
          11.0,
          error.empty() ? cream : ColorRgba8{237U, 163U, 120U, 255U});
    if (reveal) {
        fill(list, {60, 219, 680, 251}, {24U, 21U, 14U, 255U});
        label(list,
              "SUPPLY CRATE OPENED",
              {100, 237, 600, 35},
              26.0,
              gold,
              true,
              HorizontalTextAlignment::center);
        label(list,
              reveal->name,
              {100, 298, 600, 48},
              32.0,
              rarity_color(reveal->rarity),
              true,
              HorizontalTextAlignment::center);
        label(list,
              reveal->rarity + "  /  ADDED TO YOUR INVENTORY",
              {100, 352, 600, 28},
              16.0,
              cream,
              true,
              HorizontalTextAlignment::center);
        label(list,
              reveal->verified ? "Receipt verified. Your item is already saved."
                               : "Saved by AoSPlay. Receipt available in History.",
              {100, 404, 600, 28},
              12.0,
              cream,
              false,
              HorizontalTextAlignment::center);
        button(list, {515, 500, 225, 48}, "CONTINUE");
        return list;
    }
    if (section == InventorySection::packs) {
        label(list, "COMMUNITY SKIN PACKS", {80, 232, 640, 38}, 26.0, gold, true);
        const std::array lines{"Create with the game's voxel shapes and team markings.",
                               "Data-only packs: models, colour palettes and a creator manifest.",
                               "No scripts or gameplay changes. Original assets are required.",
                               "Creator guide and official pages are available on AoSPlay.",
                               "Pack installation is in design; unreviewed packs are not loaded."};
        for (std::size_t i = 0U; i < lines.size(); ++i)
            label(list, lines[i], {80, 290 + 31.0 * i, 640, 27}, 13.0);
        button(list, {515, 500, 225, 48}, "CREATOR PAGES");
        return list;
    }
    const auto items = filtered_items();
    const auto count = section == InventorySection::collection ? items.size()
                       : section == InventorySection::crates   ? data.crates.size()
                                                               : data.history.size();
    if (section == InventorySection::collection) {
        button(list, {60, 219, 123, 28}, kind_labels[kind_filter]);
        button(list, {188, 219, 123, 28}, rarities[rarity_filter]);
        button(list, {316, 219, 80, 28}, owned_only ? "OWNED" : "ALL");
    } else
        label(list,
              section == InventorySection::crates ? "EARNED THROUGH NORMAL PLAY"
                                                  : "YOUR PERMANENT OPENING RECORD",
              {65, 221, 335, 26},
              12.0,
              cream,
              true);
    for (std::size_t row = 0U; row < page_size; ++row) {
        const auto index = page * page_size + row;
        if (index >= count)
            break;
        const auto y = 251 + 31.0 * row;
        fill(list,
             {60, y, 336, 29},
             index == selected ? ColorRgba8{78U, 76U, 39U, 255U}
             : row % 2U        ? ColorRgba8{24U, 21U, 14U, 255U}
                               : ColorRgba8{57U, 53U, 44U, 255U});
        std::string name, detail, rarity;
        if (section == InventorySection::collection) {
            const auto& item = data.items[items[index]];
            name = item.name;
            rarity = item.rarity;
            detail = item.equipped ? "EQUIPPED" : item.owned ? "OWNED" : "LOCKED";
        } else if (section == InventorySection::crates) {
            name = crate_name(data.crates[index].catalog_version);
            detail = "LEVEL " + data.crates[index].level;
        } else {
            name = data.history[index].name;
            rarity = data.history[index].rarity;
            detail = data.history[index].date.substr(0, 10);
        }
        fill(list, {60, y, 3, 29}, rarity_color(rarity));
        label(list, name, {70, y, 223, 29}, 12.0, rarity_color(rarity));
        label(list, detail, {295, y, 90, 29}, 9.0, cream, false, HorizontalTextAlignment::right);
    }
    if (count == 0U)
        label(list,
              busy                                   ? "Loading your collection..."
              : section == InventorySection::crates  ? "Level up to earn your first free crate."
              : section == InventorySection::history ? "Opened crates will appear here."
                                                     : "No items match these filters.",
              {65, 286, 325, 100},
              14.0,
              cream,
              false,
              HorizontalTextAlignment::center);
    button(list, {60, 445, 70, 28}, "PREV");
    button(list, {326, 445, 70, 28}, "NEXT");
    label(list,
          "PAGE " + std::to_string(page + 1U),
          {135, 445, 185, 28},
          11.0,
          cream,
          false,
          HorizontalTextAlignment::center);
    fill(list, {420, 219, 320, 251}, {24U, 21U, 14U, 255U});
    if (section == InventorySection::collection) {
        const auto* item = selected_item();
        if (item) {
            label(list, item->name, {435, 224, 290, 25}, 20.0, rarity_color(item->rarity), true);
            if (preview_ready && item->kind != "profile_badge")
                image(list, inventory_preview_asset, {445, 253, 270, 132});
            else if (item->kind == "profile_badge")
                image(list,
                      item->asset,
                      {548, 285, 64, 64},
                      {item->palette[0], item->palette[1], item->palette[2], 255U});
            else
                label(list,
                      "Preparing voxel preview...",
                      {440, 280, 275, 65},
                      12.0,
                      cream,
                      false,
                      HorizontalTextAlignment::center);
            button(list, {435, 390, 55, 28}, "LEFT");
            button(list, {495, 390, 55, 28}, "RIGHT");
            button(list, {555, 390, 75, 28}, blue_team ? "BLUE" : "GREEN");
            button(list, {635, 390, 80, 28}, "ZOOM");
            const auto slot = item->slots.empty()
                                  ? std::string{}
                                  : item->slots[std::min(slot_index, item->slots.size() - 1U)];
            button(list,
                   {435, 425, 280, 28},
                   slot.ends_with(":view")    ? "FIRST PERSON"
                   : slot.ends_with(":world") ? "WORLD MODEL"
                                              : "APPEARANCE");
            const auto enabled = online && !busy && item->owned && data.equip_enabled;
            label(list,"By " + item->author,{435,470,290,24},10.0);
            button(list,
                   {515, 500, 225, 48},
                   equipped_item(slot) == item ? "UNEQUIP"
                   : item->owned               ? "EQUIP"
                                               : "EARN FROM CRATES",
                   enabled);
        }
    } else if (section == InventorySection::crates) {
        const auto version = selected_crate() ? selected_crate()->catalog_version : "weapons-v4";
        const auto pool = inventory_crate_pool(data,version);
        const auto odds = inventory_effective_odds(pool);
        const auto chance = [&](std::size_t tier) {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1) << odds[tier] * 100.0;
            return text.str() + "%";
        };
        if (preview_ready)
            image(list, inventory_preview_asset, {485, 219, 190, 93});
        label(list,
              crate_name(version),
              {435, 312, 290, 28},
              22.0,
              gold,
              true,
              HorizontalTextAlignment::center);
        label(list,
              "Common " + chance(0) + "  /  Uncommon " + chance(1),
              {435, 347, 290, 22},
              11.0,
              cream,
              false,
              HorizontalTextAlignment::center);
        label(list,
              "Rare " + chance(2) + "  /  Epic " + chance(3) + "  /  Legendary " + chance(4),
              {435, 369, 290, 22},
              11.0,
              cream,
              false,
              HorizontalTextAlignment::center);
        label(list,
              "Your current odds. No duplicate rewards.",
              {435, 396, 290, 22},
              11.0,
              cream,
              false,
              HorizontalTextAlignment::center);
        label(list,
              "Rare+ " + std::to_string(10U - std::min(9U, pool.pity[0])) + "  /  Epic+ " +
                  std::to_string(40U - std::min(39U, pool.pity[1])) + "  /  Legendary " +
                  std::to_string(100U - std::min(99U, pool.pity[2])),
              {435, 425, 290, 24},
              11.0,
              gold,
              false,
              HorizontalTextAlignment::center);
        const auto available = std::ranges::any_of(odds, [](double value) { return value > 0.0; });
        button(list,
               {515, 500, 225, 48},
               available ? "OPEN FREE" : "COLLECTION COMPLETE",
               available && online && !busy && data.opening_enabled && selected_crate());
    } else if (selected < data.history.size()) {
        const auto& entry = data.history[selected];
        label(list, entry.name, {435, 232, 290, 45}, 23.0, rarity_color(entry.rarity), true);
        label(list, entry.date.substr(0, 19), {435, 286, 290, 25});
        label(list, "SEED COMMITMENT", {435, 326, 290, 22}, 13.0, gold, true);
        label(list, entry.commitment.substr(0, 32), {435, 354, 290, 22}, 10.0);
        label(list, entry.commitment.substr(std::min<std::size_t>(32U, entry.commitment.size())),
              {435, 376, 290, 22}, 10.0);
        label(list, "Reward saved. Opening cannot be rerolled.", {435, 425, 290, 27}, 11.0);
    }
    return list;
}
} // namespace battlespades::frontend
