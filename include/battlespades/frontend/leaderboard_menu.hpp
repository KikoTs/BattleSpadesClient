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

/** Retail leaderboard filters in the exact DropBoxControl order. */
enum class LeaderboardType : std::uint8_t {
    general,
    team_deathmatch,
    vip,
    territory_control,
    occupation,
    diamond_mine,
    capture_the_flag,
    zombie,
    demolition,
    multihill,
};

/** Retail scope selector order. */
enum class LeaderboardScope : std::uint8_t {
    global,
    local,
    friends,
};

enum class LeaderboardLoadState : std::uint8_t {
    loading,
    ready,
    unavailable,
};

enum class LeaderboardSortDirection : std::uint8_t {
    none,
    ascending,
    descending,
};

/** Exactly one DropBoxControl may own focus in the retail menu. */
enum class LeaderboardDropdown : std::uint8_t {
    none,
    type,
    scope,
};

/** One server-independent row after the score-service adapter has formatted it. */
struct LeaderboardRow final {
    std::uint64_t account_id{};
    std::uint32_t rank{};
    std::string player_name;
    std::vector<std::string> values;

    [[nodiscard]] friend bool operator==(const LeaderboardRow&, const LeaderboardRow&) = default;
};

/** Current social persona used to mirror SteamGetFriendPersonaName. */
struct LeaderboardPersona final {
    std::uint64_t account_id{};
    std::string display_name;
};

/**
 * Replace score-service names with the signed-in/friend persona names.
 *
 * Retail performs this projection after every leaderboard response. Local
 * identity wins over a friend entry for the same account; empty and zero-ID
 * personas are inert.
 */
void apply_retail_leaderboard_personas(
    std::vector<LeaderboardRow>& rows,
    std::uint64_t local_account_id,
    std::string_view local_display_name,
    std::span<const LeaderboardPersona> friends);

/** Immutable request emitted whenever the selected cache entry is absent. */
struct LeaderboardRequest final {
    std::uint64_t generation{};
    LeaderboardType type{LeaderboardType::general};
    LeaderboardScope scope{LeaderboardScope::global};

    [[nodiscard]] friend constexpr bool operator==(const LeaderboardRequest&,
                                                   const LeaderboardRequest&) = default;
};

/** Static retail column/filter definition for one leaderboard type. */
struct LeaderboardDefinition final {
    LeaderboardType type{LeaderboardType::general};
    std::string_view filter_key;
    std::span<const std::string_view> column_keys;
};

/**
 * Renderer-neutral reconstruction of LeaderboardMenu.
 *
 * Results are cached per (type, scope), matching the retail Python client.
 * While a request is active both drop-downs are locked. Completion tokens are
 * generation checked, so a late HTTP callback can never populate a new view.
 */
class LeaderboardMenuModel final {
public:
    static constexpr std::size_t type_count{10U};
    static constexpr std::size_t scope_count{3U};
    // ListPanelBase computes thirteen slots from the 330px list area, while
    // LeaderboardListPanel deliberately stops at max_index - 1. That retail
    // off-by-one leaves twelve player rows (eleven with horizontal scrolling).
    static constexpr std::size_t visible_rows{12U};

    LeaderboardMenuModel();

    [[nodiscard]] LeaderboardType selected_type() const noexcept;
    [[nodiscard]] LeaderboardScope selected_scope() const noexcept;
    [[nodiscard]] LeaderboardLoadState state() const noexcept;
    [[nodiscard]] bool selectors_enabled() const noexcept;
    [[nodiscard]] std::span<const LeaderboardRow> rows() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] std::size_t first_visible_stat_column() const noexcept;
    [[nodiscard]] std::optional<std::size_t> sorted_column() const noexcept;
    [[nodiscard]] LeaderboardSortDirection sort_direction() const noexcept;
    [[nodiscard]] LeaderboardDropdown open_dropdown() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_row() const noexcept;
    [[nodiscard]] std::optional<std::size_t> hovered_row() const noexcept;

    /** Returns the one outstanding request once, for delivery to an HTTP adapter. */
    [[nodiscard]] std::optional<LeaderboardRequest> take_request() noexcept;

    [[nodiscard]] bool select_type(LeaderboardType type) noexcept;
    [[nodiscard]] bool select_scope(LeaderboardScope scope) noexcept;
    [[nodiscard]] bool toggle_dropdown(LeaderboardDropdown dropdown) noexcept;
    [[nodiscard]] bool close_dropdown() noexcept;
    [[nodiscard]] bool scroll_rows(int delta,
                                   std::size_t visible_capacity = visible_rows) noexcept;
    /** Scrolls only the stat columns; Rank and Name remain pinned like retail. */
    [[nodiscard]] bool scroll_stat_columns(int delta,
                                           std::size_t visible_stat_columns) noexcept;
    [[nodiscard]] bool sort_by(std::size_t column_index) noexcept;
    /** Retail row clicks select/highlight in place; they do not open a profile. */
    [[nodiscard]] bool select_row(std::size_t row_index) noexcept;
    /** Mirrors ListPanelBase's per-row hover flag without changing selection. */
    [[nodiscard]] bool hover_row(std::optional<std::size_t> row_index) noexcept;

    /** Applies a successful response; stale and malformed-width rows fail closed. */
    [[nodiscard]] bool complete(LeaderboardRequest request,
                                std::vector<LeaderboardRow> rows) noexcept;

    /** Retail shows an empty table after an unsuccessful score-server response. */
    [[nodiscard]] bool fail(LeaderboardRequest request) noexcept;

    /** Clears all endpoint-derived state when the signed-in account changes. */
    void invalidate() noexcept;

private:
    struct CacheEntry final {
        bool populated{};
        std::vector<LeaderboardRow> rows;
    };

    [[nodiscard]] CacheEntry& selected_cache() noexcept;
    [[nodiscard]] const CacheEntry& selected_cache() const noexcept;
    void activate_selection() noexcept;
    void reset_sort_and_scroll() noexcept;
    void reset_row_interaction() noexcept;
    [[nodiscard]] std::optional<std::size_t>
    row_index_for_account(std::optional<std::uint64_t> account_id) const noexcept;

    std::array<std::array<CacheEntry, scope_count>, type_count> cache_{};
    LeaderboardType selected_type_{LeaderboardType::general};
    LeaderboardScope selected_scope_{LeaderboardScope::global};
    LeaderboardLoadState state_{LeaderboardLoadState::loading};
    std::optional<LeaderboardRequest> pending_request_;
    bool request_taken_{};
    std::uint64_t next_generation_{1U};
    std::size_t first_visible_row_{};
    std::size_t first_visible_stat_column_{};
    std::optional<std::size_t> sorted_column_;
    LeaderboardSortDirection sort_direction_{LeaderboardSortDirection::none};
    LeaderboardDropdown open_dropdown_{LeaderboardDropdown::none};
    std::optional<std::uint64_t> selected_account_id_;
    std::optional<std::uint64_t> hovered_account_id_;
};

[[nodiscard]] std::span<const LeaderboardDefinition> leaderboard_definitions() noexcept;
[[nodiscard]] const LeaderboardDefinition& leaderboard_definition(LeaderboardType type) noexcept;

} // namespace battlespades::frontend
