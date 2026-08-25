#pragma once

#include "battlespades/frontend/ugc_prefab_catalog.hpp"
#include "battlespades/ui/geometry.hpp"
#include "battlespades/world/class_selection.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** The two retail SelectUGC specialisations opened from EscapeMenu. */
enum class UgcLoadoutLibrary : std::uint8_t {
    constructs,
    game_data,
};

enum class UgcLoadoutItemKind : std::uint8_t {
    prefab,
    game_data,
};

enum class UgcLoadoutAction : std::uint8_t {
    submit,
    back,
};

/** HUD-zone cues emitted by the recovered SelectUGC interaction paths. */
enum class UgcLoadoutAudioCue : std::uint8_t {
    scroll,
    confirm,
    back,
};

/**
 * Entity ids unlocked by one source-authored UGC objective identifier.
 *
 * Packet 68 carries string objective ids rather than marker ids. Retail maps
 * those ids through UGC_OBJECTIVES_TYPES before constructing SelectGameData;
 * exposing the same lookup here keeps the native frontend from guessing from
 * mode numbers or from whatever entities happen to be placed already.
 */
[[nodiscard]] std::span<const std::uint8_t>
ugc_loadout_entity_ids_for_objective(std::string_view objective_id) noexcept;

/** One item in the shared five-slot Construct/Game Data backpack. */
struct UgcLoadoutChoice final {
    UgcLoadoutItemKind kind{UgcLoadoutItemKind::prefab};
    std::string prefab_name;
    std::uint8_t ugc_tool{};

    [[nodiscard]] friend bool operator==(const UgcLoadoutChoice&,
                                         const UgcLoadoutChoice&) = default;
};

/** Presentation data retained from the source-authored UGC catalogs. */
struct UgcLoadoutItem final {
    UgcLoadoutChoice choice;
    /** Retail localization key (prefab identifier or A482..A500). */
    std::string label_key;
    std::string preview_asset;
    /** Construct-only Tiny/Small/Medium/Large/Huge source label. */
    std::string size_label_key;
};

/** One packet-68 validation row resolved through UGC_OBJECTIVES_TYPES. */
struct UgcLoadoutObjective final {
    std::string id;
    std::int32_t value{};
    std::int32_t minimum{};
    std::int32_t maximum{};
    std::uint8_t priority{};

    [[nodiscard]] bool complete() const noexcept {
        return value >= minimum && value <= maximum;
    }
};

/** Resolve a packet-68 row to its exact source min/max/priority contract. */
[[nodiscard]] std::optional<UgcLoadoutObjective>
ugc_loadout_objective(std::string_view objective_id,
                      std::int32_t value) noexcept;

struct UgcLoadoutTab final {
    std::string label_key;
    std::vector<UgcLoadoutItem> items;
    std::size_t first_visible_item{};
};

/**
 * Renderer-neutral model for SelectPrefabs and SelectGameData.
 *
 * Ownership: one active multiplayer frontend. Inputs are the exact prefab
 * names advertised by InitialInfo and the marker ids enabled by UGC objective
 * metadata. The model never invents unknown prefabs or objective markers.
 * SelectUGC's five-entry inventory is shared by both libraries; choosing a
 * sixth item evicts the oldest entry exactly as the Python screen does.
 */
class UgcLoadoutMenuModel final {
public:
    static constexpr std::size_t maximum_selection{5U};

    void configure(UgcLoadoutLibrary library,
                   std::span<const std::string> server_prefabs,
                   std::span<const std::uint8_t> allowed_ugc_tools,
                   std::span<const std::string> selected_prefabs,
                   std::span<const std::uint8_t> selected_ugc_tools,
                   std::uint8_t team,
                   bool in_game,
                   std::span<const UgcLoadoutObjective> objectives = {});

    void open_library(UgcLoadoutLibrary library) noexcept;

    [[nodiscard]] UgcLoadoutLibrary library() const noexcept { return library_; }
    [[nodiscard]] std::uint8_t team() const noexcept { return team_; }
    [[nodiscard]] bool in_game() const noexcept { return in_game_; }
    [[nodiscard]] std::span<const UgcLoadoutTab> tabs() const noexcept;
    [[nodiscard]] std::size_t current_tab_index() const noexcept;
    [[nodiscard]] const UgcLoadoutTab* current_tab() const noexcept;
    [[nodiscard]] std::span<const UgcLoadoutItem> visible_items() const noexcept;
    [[nodiscard]] std::size_t columns() const noexcept;
    [[nodiscard]] std::size_t items_per_page() const noexcept;
    [[nodiscard]] std::span<const UgcLoadoutChoice> inventory() const noexcept;
    [[nodiscard]] std::span<const UgcLoadoutObjective> objectives() const noexcept {
        return objectives_;
    }
    [[nodiscard]] const UgcLoadoutItem*
    item(const UgcLoadoutChoice& choice) const noexcept;
    [[nodiscard]] bool selected(const UgcLoadoutChoice& choice) const noexcept;
    [[nodiscard]] world::ClassSelection selection() const;
    [[nodiscard]] std::optional<ui::Point> hovered() const noexcept { return hovered_; }
    [[nodiscard]] std::optional<UgcLoadoutAudioCue> take_audio_cue() noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<UgcLoadoutAction> click(ui::Point point);
    void select_tab(std::size_t index) noexcept;
    void scroll_rows(int direction) noexcept;
    void select_item(const UgcLoadoutChoice& choice);
    void remove_inventory(std::size_t index) noexcept;
    void set_objectives(std::span<const UgcLoadoutObjective> objectives);

    [[nodiscard]] static ui::Rect select_bounds() noexcept;
    [[nodiscard]] static ui::Rect back_bounds(bool in_game) noexcept;
    [[nodiscard]] static ui::Rect inventory_bounds(std::size_t index) noexcept;
    [[nodiscard]] ui::Rect tab_bounds(std::size_t index) const noexcept;
    [[nodiscard]] ui::Rect visible_item_bounds(std::size_t index) const noexcept;

private:
    [[nodiscard]] std::vector<UgcLoadoutTab>& active_tabs() noexcept;
    [[nodiscard]] const std::vector<UgcLoadoutTab>& active_tabs() const noexcept;
    void build_construct_tabs(std::span<const std::string> server_prefabs);
    void build_game_data_tabs(std::span<const std::uint8_t> allowed_ugc_tools);
    void rebuild_inventory(std::span<const std::string> selected_prefabs,
                           std::span<const std::uint8_t> selected_ugc_tools);
    [[nodiscard]] bool available(const UgcLoadoutChoice& choice) const noexcept;

    UgcLoadoutLibrary library_{UgcLoadoutLibrary::constructs};
    std::uint8_t team_{2U};
    bool in_game_{};
    std::vector<UgcLoadoutTab> construct_tabs_;
    std::vector<UgcLoadoutTab> game_data_tabs_;
    std::size_t construct_tab_index_{};
    std::size_t game_data_tab_index_{};
    std::vector<UgcLoadoutChoice> inventory_;
    std::vector<UgcLoadoutObjective> objectives_;
    std::optional<ui::Point> hovered_;
    std::optional<UgcLoadoutAudioCue> pending_audio_cue_;
};

} // namespace battlespades::frontend
