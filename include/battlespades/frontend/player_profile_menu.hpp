#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

enum class PlayerProfileTab : std::uint8_t {
    player_stats,
    game_modes,
    classes,
    equipment,
    inventory,
};

enum class PlayerProfileLoadState : std::uint8_t {
    loading,
    ready,
    not_found,
};

enum class PlayerProfileRowKind : std::uint8_t {
    category,
    statistic,
    summary_rank,
};

/** Raw values printed over a retail rank-progress bar. */
struct PlayerProfileLevelDetails final {
    std::uint32_t level{1U};
    double current{};
    double next_level_min{};
    double next_level_max{};

    [[nodiscard]] friend constexpr bool operator==(const PlayerProfileLevelDetails&,
                                                   const PlayerProfileLevelDetails&) = default;
};

struct PlayerProfileRow final {
    PlayerProfileRowKind kind{PlayerProfileRowKind::statistic};
    std::string category_key;
    std::string label_key;
    std::string value;
    std::optional<double> level_progress;
    std::optional<PlayerProfileLevelDetails> level_details;

    [[nodiscard]] friend bool operator==(const PlayerProfileRow&,
                                         const PlayerProfileRow&) = default;
};

struct PlayerProfileData final {
    std::string player_name;
    double kill_death_ratio{};
    std::array<std::vector<PlayerProfileRow>, 4U> rows;
};

struct PlayerProfileRequest final {
    std::uint64_t generation{};
    std::uint64_t account_id{};

    [[nodiscard]] friend constexpr bool operator==(const PlayerProfileRequest&,
                                                   const PlayerProfileRequest&) = default;
};

struct PlayerProfileTabDefinition final {
    PlayerProfileTab tab{PlayerProfileTab::player_stats};
    std::string_view label_key;
    std::span<const std::string_view> filter_keys;
};

/**
 * Renderer-neutral PlayerProfileMenu state and stale-callback boundary.
 *
 * Filter index zero means All; subsequent values address the exact recovered
 * per-tab filter list. Retail reconstructs the drop-down on every tab change,
 * so returning to a tab resets its filter to All rather than remembering it.
 *
 * Retail's ACHIEVEMENTS button opened Steam's overlay page. Steam only ever
 * let the publisher's servers write those achievements, so the overlay cannot
 * show what a player earns now; the button opens a list in this menu instead.
 */
class PlayerProfileMenuModel final {
public:
    static constexpr std::size_t tab_count{5U};
    static constexpr std::size_t summary_visible_rows{12U};
    static constexpr std::size_t statistic_visible_rows{13U};
    // Compatibility alias for callers that only need the largest row count.
    static constexpr std::size_t visible_rows{statistic_visible_rows};
    static constexpr std::size_t achievement_visible_rows{6U};

    explicit PlayerProfileMenuModel(std::uint64_t account_id = 0U);

    [[nodiscard]] PlayerProfileTab selected_tab() const noexcept;
    [[nodiscard]] PlayerProfileLoadState state() const noexcept;
    [[nodiscard]] std::uint64_t account_id() const noexcept;
    [[nodiscard]] const PlayerProfileData* data() const noexcept;
    [[nodiscard]] std::size_t selected_filter() const noexcept;
    [[nodiscard]] std::span<const PlayerProfileRow> displayed_rows() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] std::size_t visible_row_capacity() const noexcept;
    [[nodiscard]] bool filter_visible() const noexcept;
    [[nodiscard]] bool filter_open() const noexcept;

    [[nodiscard]] std::optional<PlayerProfileRequest> take_request() noexcept;
    [[nodiscard]] bool select_tab(PlayerProfileTab tab);
    [[nodiscard]] bool toggle_filter() noexcept;
    [[nodiscard]] bool close_filter() noexcept;
    [[nodiscard]] bool select_filter(std::size_t index);
    [[nodiscard]] bool scroll_rows(int delta) noexcept;
    /** The achievements list is showing in place of the statistics. */
    [[nodiscard]] bool achievements_open() const noexcept;
    [[nodiscard]] std::size_t first_visible_achievement() const noexcept;
    /** The ACHIEVEMENTS button: shows the list, or returns to the statistics. */
    void activate_achievements() noexcept;
    /** Scrolls a list of `total` achievements; false at either end. */
    [[nodiscard]] bool scroll_achievements(int delta, std::size_t total) noexcept;
    [[nodiscard]] bool complete(PlayerProfileRequest request, PlayerProfileData data);
    [[nodiscard]] bool fail(PlayerProfileRequest request) noexcept;
    void reload(std::uint64_t account_id) noexcept;

private:
    void rebuild_display_rows();

    std::uint64_t account_id_{};
    std::uint64_t next_generation_{1U};
    PlayerProfileTab selected_tab_{PlayerProfileTab::player_stats};
    PlayerProfileLoadState state_{PlayerProfileLoadState::loading};
    std::array<std::size_t, tab_count> selected_filters_{};
    std::optional<PlayerProfileData> data_;
    std::vector<PlayerProfileRow> displayed_rows_;
    std::optional<PlayerProfileRequest> pending_request_;
    bool request_taken_{};
    std::size_t first_visible_row_{};
    bool filter_open_{};
    bool achievements_open_{};
    std::size_t first_visible_achievement_{};
};

[[nodiscard]] std::span<const PlayerProfileTabDefinition> player_profile_tab_definitions() noexcept;
[[nodiscard]] const PlayerProfileTabDefinition&
player_profile_tab_definition(PlayerProfileTab tab) noexcept;

} // namespace battlespades::frontend
