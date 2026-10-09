#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
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

/** Retail FLAREBLOCK_TOOL and PREFAB_TOOL. */
inline constexpr std::uint8_t flare_block_tool{22U};
inline constexpr std::uint8_t prefab_tool{23U};

/**
 * Synthetic Constructs tile for FLAREBLOCK_TOOL. Retail SelectClass puts the
 * flare block first in the prefab TableSelection; picking it adds tool 22 to
 * the loadout instead of a prefab name (selectClass.py create_loadout_list).
 */
inline constexpr std::string_view flare_block_construct{"flareblock"};

/**
 * One class's last confirmed choice: retail config keys `loadout<N>` (the tool
 * ids SelectClass.create_loadout_list stored) and `prefabs<N>`. `prefabs` also
 * carries `flare_block_construct` when the flare tile was chosen.
 */
struct SavedClassLoadout final {
    std::vector<std::uint8_t> loadout;
    std::vector<std::string> prefabs;

    [[nodiscard]] friend bool operator==(const SavedClassLoadout&,
                                         const SavedClassLoadout&) = default;
};

/**
 * Everything the retail picker reads besides the class tables: the server's
 * rules (InitialInfo.disabled_tools, the manager's mafia/UGC mode, the map's
 * prefab catalogue) and the player's saved choices. Defaults describe an
 * unrestricted server and a player who never picked anything.
 */
struct ClassSelectionRules final {
    std::vector<std::uint8_t> disabled_tools;
    /** InitialInfo replacements, keyed by (class id, CLASS_ITEMS group). */
    std::map<std::pair<std::uint8_t, std::uint8_t>, std::vector<std::uint8_t>> loadout_overrides;
    /** manager.is_in_mafia_mode(): no flare/prefab tool, 0 default constructs. */
    bool mafia{};
    /** game_mode == UGC: 0 default constructs. */
    bool ugc{};
    /**
     * StateData.prefabs, which GameScene.process_packet_state_data stores as
     * prefab_manager.map_prefabs. Names the client cannot show (not a plain
     * prefab stem) are ignored.
     */
    std::vector<std::string> map_prefabs;
    /** GameClass(config): the saved choice of each class, by class id. */
    std::map<std::uint8_t, SavedClassLoadout> saved;

    [[nodiscard]] bool tool_disabled(std::uint16_t tool) const noexcept;
    [[nodiscard]] const SavedClassLoadout* saved_loadout(std::uint8_t class_id) const noexcept;
};

/**
 * Whether CLASS_ITEMS[class][CLASS_PREFABS] lists MAP_PREFABS. Every stock
 * class does except the three zombies and the classic soldier.
 */
[[nodiscard]] bool class_offers_map_prefabs(std::uint8_t class_id) noexcept;

/** A name SelectClass can show: a plain prefab stem of letters, digits and '_'. */
[[nodiscard]] bool valid_map_prefab_name(std::string_view name) noexcept;

/** selectClass.is_item_map_prefab: a case-insensitive match against the map's list. */
[[nodiscard]] bool is_map_prefab(std::string_view name, const ClassSelectionRules& rules) noexcept;

/**
 * GameClass.get_valid_items per loadout row: the index (into class_row_options)
 * of the first row item found in the saved loadout, else 0.
 */
[[nodiscard]] std::array<std::size_t, 4U>
saved_row_indices(std::uint8_t class_id, const ClassSelectionRules& rules);

/** Construct names allowed by CLASS_ITEMS/PREFAB_LISTS (server tables) for one class. */
[[nodiscard]] std::span<const std::string_view>
class_prefab_options(std::uint8_t class_id) noexcept;

/** One retail loadout row (melee/primary/secondary/equipment) minus disabled tools. */
[[nodiscard]] std::vector<std::uint16_t>
class_row_options(std::uint8_t class_id, std::size_t group, const ClassSelectionRules& rules);

/**
 * SelectClass.get_class_images(CLASS_PREFABS): the flare tile first (unless the
 * class is a zombie/Deuce or flare is disabled), then the class constructs
 * that are not also map prefabs, then the map's prefabs (for a class that
 * lists MAP_PREFABS). No constructs when PREFAB_TOOL is disabled.
 */
[[nodiscard]] std::vector<std::string>
class_construct_options(std::uint8_t class_id, const ClassSelectionRules& rules);

/**
 * The constructs a class starts with: none in mafia/UGC or when PREFAB_TOOL is
 * disabled; otherwise the saved constructs the class still offers, and
 * without any, GameClass.get_prefabs' first three class constructs (only when
 * at least three exist).
 *
 * Retail writes `prefabs<N>` on every confirmed choice but reads it back only
 * in UGC mode (gameClass.pyc get_prefabs: `config and not game_mode != UGC`),
 * so its constructs reset to the first three on each visit. Reading the saved
 * constructs in every mode is a deliberate improvement.
 */
[[nodiscard]] std::vector<std::string>
default_class_constructs(std::uint8_t class_id, const ClassSelectionRules& rules);

/**
 * Build SetClassLoadout from explicit row items plus constructs, exactly as
 * create_loadout_list + set_common_loadout_items do: BLOCK_TOOL first, the
 * chosen rows, then CLASS_COMMON minus disabled tools; tool 22 only when the
 * flare tile is chosen.
 */
[[nodiscard]] ClassSelection make_class_selection(
    std::uint8_t class_id,
    std::span<const std::uint16_t> chosen_items,
    std::span<const std::string> constructs,
    const ClassSelectionRules& rules);

/**
 * GameClass(...).loadout for a class the player did not pick on this visit
 * (locked-class teams, mafia auto-pick): per row the saved item, else the
 * first enabled one; common tools; and FLARE/PREFAB appended outside mafia
 * mode (gameClass.build_class_loadout).
 */
[[nodiscard]] ClassSelection automatic_class_selection(std::uint8_t class_id,
                                                       const ClassSelectionRules& rules);

/** Build a valid one-choice-per-row selection, including three prefabs. */
[[nodiscard]] ClassSelection default_class_selection(std::uint8_t class_id);

/** Build from explicit row/construct choices; never refill deselected prefabs. */
[[nodiscard]] ClassSelection make_class_selection(
    std::uint8_t class_id,
    const std::array<std::size_t, 4U>& option_indices,
    std::span<const std::string> prefabs);

/**
 * The selection a live match's first life is built from: the loadout the
 * server committed in the local CreatePlayer whenever it carried one, and the
 * request we sent only when it did not. The server normalizes requests, and
 * Zombie mode replaces them outright (the infected get 24, 28, 23 and no
 * Flare Block), so building from the request handed a zombie a tool the server
 * refuses and that has no FPS arms to hold it.
 */
[[nodiscard]] inline ClassSelection live_spawn_selection(const ClassSelection& committed,
                                                         const ClassSelection* requested) {
    if (committed.loadout.empty() && requested != nullptr) return *requested;
    return committed;
}

} // namespace battlespades::world
