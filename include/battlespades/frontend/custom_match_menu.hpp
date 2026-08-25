#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** Retail filter order used by `BaseSquadsMenu.server_type_filter`. */
enum class CustomMatchSource : std::uint8_t {
    friends,
    open,
};

/** Explicit discovery state; an empty result is not represented by fake rows. */
enum class CustomMatchDiscoveryState : std::uint8_t {
    offline,
    idle,
    refreshing,
    ready,
    error,
};

/** Immutable lobby metadata supplied by a Steam/AoSPlay discovery adapter. */
struct CustomMatchLobbyRecord final {
    std::string lobby_id;
    std::string name;
    std::uint16_t member_count{};
    std::uint16_t maximum_members{};
    std::uint16_t friend_count{};
    std::uint16_t ping_milliseconds{};
    std::vector<std::string> game_info;
    std::vector<std::string> game_rules;
    bool open{true};
    bool content_owned{true};

    [[nodiscard]] bool full() const noexcept;
    [[nodiscard]] bool details_ready() const noexcept;
};

/** Side effects owned by matchmaking/navigation services, never by the menu. */
enum class CustomMatchIntentKind : std::uint8_t {
    refresh_lobbies,
    join_lobby,
    create_lobby,
    back_to_join_match,
};

struct CustomMatchIntent final {
    CustomMatchIntentKind kind{CustomMatchIntentKind::refresh_lobbies};
    CustomMatchSource source{CustomMatchSource::open};
    std::string lobby_id;

    [[nodiscard]] friend bool operator==(const CustomMatchIntent&,
                                         const CustomMatchIntent&) = default;
};

/** Minimal route state required when create/join succeeds. */
enum class CustomMatchLobbyRole : std::uint8_t {
    owner,
    member,
};

enum class CustomMatchLobbyBackRoute : std::uint8_t {
    select_menu,
    custom_match_list,
};

struct CustomMatchLobbyRootState final {
    std::string lobby_id;
    CustomMatchLobbyRole role{CustomMatchLobbyRole::member};

    [[nodiscard]] CustomMatchLobbyBackRoute back_route() const noexcept;
    [[nodiscard]] std::string_view title_localization_key() const noexcept;
    [[nodiscard]] std::string_view settings_title_localization_key() const noexcept;
};

enum class CustomMatchControlKind : std::uint8_t {
    navigation_item,
    source_filter,
    join_button,
};

struct CustomMatchControl final {
    ui::Widget widget{};
    CustomMatchControlKind kind{CustomMatchControlKind::navigation_item};
    std::string_view localization_key;
};

/**
 * Renderer-neutral reconstruction of retail `MatchSquadsMenu`.
 *
 * The model owns only bounded list/filter/selection state. Enumeration,
 * Steam callbacks, AoSPlay requests, audio and scene changes are explicit
 * typed intents. All methods run synchronously on the fixed UI tick.
 */
class CustomMatchMenuModel final {
public:
    static constexpr std::size_t control_count{3U};
    static constexpr std::size_t maximum_lobbies{4'096U};
    static constexpr std::size_t visible_lobby_rows{13U};

    CustomMatchMenuModel();

    [[nodiscard]] std::span<const CustomMatchControl> controls() const noexcept;
    [[nodiscard]] std::span<const CustomMatchLobbyRecord> lobbies() const noexcept;
    [[nodiscard]] const CustomMatchLobbyRecord* selected_lobby() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] CustomMatchSource source() const noexcept;
    [[nodiscard]] CustomMatchDiscoveryState discovery_state() const noexcept;

    /** Disconnect clears stale rows so the screen cannot offer a phantom join. */
    void set_network_available(bool available) noexcept;

    /** Starts one bounded discovery request; duplicate in-flight refreshes are ignored. */
    [[nodiscard]] std::optional<CustomMatchIntent> request_refresh() noexcept;

    /**
     * Completes the active refresh transaction.
     *
     * Malformed/duplicate rows are discarded. Failure clears stale discovery
     * results and exposes an honest error state.
     */
    [[nodiscard]] bool complete_refresh(std::vector<CustomMatchLobbyRecord> lobbies,
                                        bool success);

    /** Retail filter changes repopulate the list and immediately enumerate again. */
    [[nodiscard]] std::optional<CustomMatchIntent>
    set_source(CustomMatchSource source) noexcept;
    [[nodiscard]] std::optional<CustomMatchIntent> cycle_source(int direction) noexcept;

    [[nodiscard]] bool select_lobby(std::size_t index) noexcept;
    [[nodiscard]] bool select_visible_row(std::size_t row) noexcept;
    void scroll_rows(int delta) noexcept;

    [[nodiscard]] bool can_join() const noexcept;
    [[nodiscard]] bool selected_requires_purchase() const noexcept;
    [[nodiscard]] std::optional<CustomMatchIntent> request_join() const;

    /**
     * Host/create is intentionally not a visible MatchSquadsMenu button.
     * Retail exposes it in the base class and reaches hosting through the
     * Create Match flow; this typed boundary lets that flow share the lobby
     * service without falsifying the recovered Custom Match layout.
     */
    [[nodiscard]] std::optional<CustomMatchIntent> request_create() const;
    [[nodiscard]] CustomMatchIntent request_back() const;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<CustomMatchIntent>
    pointer_release(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<CustomMatchIntent> handle(ui::InputEvent event) noexcept;

    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::string_view status_localization_key() const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> control_index(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hit_control(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<std::size_t> hit_visible_row(ui::Point point) const noexcept;
    [[nodiscard]] bool valid_lobby(const CustomMatchLobbyRecord& lobby) const noexcept;
    void clear_discovery() noexcept;
    void repair_selection(std::optional<std::string_view> preferred_id = std::nullopt) noexcept;
    void reveal_selection() noexcept;
    void update_control_states() noexcept;

    std::array<CustomMatchControl, control_count> controls_{};
    std::vector<CustomMatchLobbyRecord> lobbies_{};
    std::optional<std::string> selected_lobby_id_{};
    std::size_t first_visible_row_{};
    CustomMatchSource source_{CustomMatchSource::open};
    CustomMatchDiscoveryState discovery_state_{CustomMatchDiscoveryState::offline};
    std::optional<ui::WidgetId> hovered_{};
    bool pointer_down_{};
    bool navigation_item_armed_{};
};

[[nodiscard]] std::string_view localization_key(CustomMatchSource source) noexcept;

} // namespace battlespades::frontend
