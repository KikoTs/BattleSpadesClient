#pragma once
#include "battlespades/network/game_protocol.hpp"

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/focus_navigator.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** Destinations owned by the retail Join Match branch. */
enum class JoinMatchRoute : std::uint8_t {
    server_browser,
    direct_connect,
    custom_match,
    random_match,
    select_menu,
};

enum class JoinMatchControlKind : std::uint8_t {
    text_button,
    navigation_item,
};

struct JoinMatchControl final {
    ui::Widget widget{};
    JoinMatchRoute route{JoinMatchRoute::server_browser};
    JoinMatchControlKind kind{JoinMatchControlKind::text_button};
    std::string_view localization_key;
    std::string_view icon_asset;
};

/**
 * Renderer-neutral reconstruction of `joinMatchMenu.py`.
 *
 * Coordinates use the same eighth-pixel, top-left 800x600 canvas as the
 * Select Menu. The three forward routes are deliberately local to this model;
 * application code decides which cached screen instance services each route.
 */
class JoinMatchMenuModel final {
public:
    static constexpr std::size_t control_count{4U};

    JoinMatchMenuModel();

    [[nodiscard]] std::span<const JoinMatchControl> controls() const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hovered() const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> focused() const noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<JoinMatchRoute>
    pointer_release(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<JoinMatchRoute> handle(ui::InputEvent event) noexcept;

    /** Disables all online routes after malformed/invalid retail data. */
    void set_online_routes_enabled(bool enabled) noexcept;
    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> index_of(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hit_test(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<JoinMatchRoute> focused_route() const noexcept;
    void rebuild_focus() noexcept;

    std::array<JoinMatchControl, control_count> controls_{};
    ui::FocusNavigator focus_{};
    std::optional<ui::WidgetId> hovered_{};
    bool pointer_down_{};
    bool navigation_item_armed_{};
};

enum class ServerBrowserSource : std::uint8_t {
    all,
    official,
    community,
    favourites,
    history,
    friends,
    local,
};

enum class ServerSortColumn : std::uint8_t {
    name,
    players,
    map,
    mode,
    ping,
};

/** Region identifiers persisted by the retail server browser. */
enum class ServerBrowserRegion : std::uint8_t {
    us_west,
    us_east,
    europe,
    australia,
};

/**
 * One asynchronous discovery request emitted by the renderer-neutral model.
 *
 * Internet All, Official, and Community requests carry the selected region.
 * Steam's favourites/history/friends and the LAN source ignore regions. The
 * monotonically changing generation lets adapters reject callbacks from a
 * cancelled source before they can contaminate the active list.
 */
struct ServerBrowserRefreshRequest final {
    std::uint64_t generation{};
    ServerBrowserSource source{ServerBrowserSource::all};
    std::optional<ServerBrowserRegion> region{};

    [[nodiscard]] friend bool operator==(const ServerBrowserRefreshRequest&,
                                         const ServerBrowserRefreshRequest&) = default;
};

/** Immutable discovery result accepted by the browser model. */
struct ServerBrowserEntry final {
    std::string name;
    std::string address;
    std::uint16_t game_port{};
    std::uint16_t query_port{};
    std::uint16_t ping_milliseconds{};
    std::string map;
    std::string mode;
    std::string mode_id;
    std::string mode_description;
    std::string region;
    std::string texture_skin;
    std::uint16_t players{};
    std::uint16_t maximum_players{};
    bool classic{};
    bool official{};
    bool favourite{};
    bool in_history{};
    bool friend_hosted{};
    bool local{};
    bool compatible{true};
    bool content_owned{true};
    std::string identity_server_id;
    bool identity_ticket{};
    /** The host's Steam id when the listing had one; zero means relay only. */
    std::uint64_t steam_host_id{};
    /**
     * Players who are not bots, when the listing separates them.
     *
     * `players` counts bots, so a bot-filled server reads as full and never
     * appears to change. Defaults to `players` for a listing that says nothing,
     * which is the old behaviour rather than a claim of zero humans.
     */
    std::uint16_t human_players{};
    /** The listing says the server asks for a password; drawn as a padlock. */
    bool password_protected{};
    network::GameProtocol protocol{network::GameProtocol::retail168};
    bool ping_known{true};

    [[nodiscard]] std::string identifier() const;
};

/** Exact loading handoff retained independently from the network adapter. */
struct ServerConnectRequest final {
    std::string identifier;
    std::string host;
    std::uint16_t port{};
    std::string expected_map;
    std::string expected_mode;
    std::string expected_skin;
    bool expected_classic{};
    std::string identity_server_id;
    bool identity_ticket{};
    /**
     * Prefer Valve's relays to reach this host when it published a Steam id.
     * The loader falls back to `host`:`port` when Steam cannot carry it, so a
     * player without Steam still joins through the AoSPlay relay.
     *
     * Last so the positional initialisers throughout the frontend keep
     * compiling and simply leave it zero.
     */
    std::uint64_t steam_host_id{};
    /**
     * A server password the player already supplied (`--password`, a join
     * link's `?password=`). It answers the server's first request; empty
     * leaves the answer to the loading screen's prompt.
     */
    std::string password;
    network::GameProtocol protocol{network::GameProtocol::automatic};
};

enum class DirectConnectActionKind : std::uint8_t {
    connect,
    add_favourite,
    back,
};

struct DirectConnectAction final {
    DirectConnectActionKind kind{DirectConnectActionKind::connect};
    std::string endpoint;
};

/**
 * Retail `InputServer` state: one focused IP:PORT edit box, Connect and Back.
 * Endpoint parsing and DNS remain network-adapter responsibilities.
 */
class DirectConnectMenuModel final {
public:
    static constexpr std::size_t maximum_endpoint_bytes{255U};

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<DirectConnectAction>
    pointer_release(std::optional<ui::Point> point) noexcept;

    [[nodiscard]] bool append_character(char character);
    /** Committed keyboard text or a complete paste; invalid input is never partially inserted. */
    [[nodiscard]] bool append_text(std::string_view text);
    [[nodiscard]] bool paste_text(std::string_view text);
    [[nodiscard]] bool erase_character() noexcept;
    void clear() noexcept;
    void focus_input() noexcept;
    [[nodiscard]] bool input_focused() const noexcept;
    [[nodiscard]] std::string_view endpoint() const noexcept;
    [[nodiscard]] std::optional<DirectConnectAction> submit() const;
    /** Add the typed endpoint to the durable Favourites source without joining. */
    [[nodiscard]] std::optional<DirectConnectAction> submit_favourite() const;
    void set_error(std::string message);
    /** Shows a non-error confirmation without styling it as a failed connection. */
    void set_notice(std::string message);
    [[nodiscard]] std::string_view error() const noexcept;
    [[nodiscard]] bool message_is_error() const noexcept;

    [[nodiscard]] WidgetVisualState connect_state() const noexcept;
    [[nodiscard]] WidgetVisualState favourite_state() const noexcept;
    [[nodiscard]] WidgetVisualState back_state() const noexcept;
    [[nodiscard]] bool input_hovered() const noexcept;

private:
    std::string endpoint_;
    std::string error_;
    bool message_is_error_{true};
    std::optional<ui::Point> pointer_;
    std::optional<DirectConnectActionKind> armed_action_;
    bool pointer_down_{};
    bool input_focused_{true};
};

/**
 * Deterministic state and filtering for the retail server browser.
 *
 * Discovery services may replace or upsert entries from any master-server
 * implementation. The model deduplicates `(address, game_port)`, rejects
 * incompatible rows, preserves a selected identity through filtering/sorting,
 * and never starts a connection itself.
 */
class ServerBrowserModel final {
public:
    static constexpr std::size_t source_count{7U};
    static constexpr std::size_t region_count{4U};
    static constexpr std::size_t retail_visible_row_count{15U};

    void replace_servers(std::vector<ServerBrowserEntry> servers);
    [[nodiscard]] bool upsert(ServerBrowserEntry server);
    void clear() noexcept;

    [[nodiscard]] std::span<const ServerBrowserEntry> servers() const noexcept;
    [[nodiscard]] std::span<const std::size_t> visible_indices() const noexcept;
    [[nodiscard]] const ServerBrowserEntry* selected() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_visible_row() const noexcept;

    [[nodiscard]] bool select_visible_row(std::size_t row) noexcept;
    void clear_selection() noexcept;
    void set_source(ServerBrowserSource source);
    void cycle_source(int direction);
    [[nodiscard]] ServerBrowserSource source() const noexcept;

    void set_region(ServerBrowserRegion region) noexcept;
    void cycle_region(int direction) noexcept;
    [[nodiscard]] ServerBrowserRegion region() const noexcept;
    [[nodiscard]] bool region_tabs_visible() const noexcept;

    /** Clears the old result set and begins a generation-checked refresh. */
    [[nodiscard]] ServerBrowserRefreshRequest begin_refresh();
    /** Accepts only callbacks belonging to the current in-flight generation. */
    [[nodiscard]] bool accept_response(std::uint64_t generation, ServerBrowserEntry server);
    [[nodiscard]] bool finish_refresh(std::uint64_t generation) noexcept;
    [[nodiscard]] bool refreshing() const noexcept;
    [[nodiscard]] std::uint64_t refresh_generation() const noexcept;

    void set_show_full_servers(bool value);
    void set_show_empty_servers(bool value);
    [[nodiscard]] bool show_full_servers() const noexcept;
    [[nodiscard]] bool show_empty_servers() const noexcept;

    /** Selecting the active column toggles direction, matching ListGrid. */
    void select_sort_column(ServerSortColumn column);
    [[nodiscard]] ServerSortColumn sort_column() const noexcept;
    [[nodiscard]] bool sort_descending() const noexcept;
    /**
     * serverInfo.py sorts MODE on the localised title, not the string id.
     * Without a lookup the id itself is compared.
     */
    void set_mode_title_lookup(std::function<std::string(std::string_view)> lookup);

    [[nodiscard]] bool toggle_selected_favourite() noexcept;
    [[nodiscard]] bool can_connect() const noexcept;
    [[nodiscard]] std::optional<ServerConnectRequest> connect_request() const;

    /**
     * Selects a row and returns a connect request only for a valid second
     * activation. `click_count` comes from the platform pointer event so OS
     * timing and movement thresholds remain authoritative.
     */
    [[nodiscard]] std::optional<ServerConnectRequest>
    activate_visible_row(std::size_t row, std::uint8_t click_count);

    void set_visible_row_capacity(std::size_t row_count) noexcept;
    [[nodiscard]] std::size_t visible_row_capacity() const noexcept;
    void set_first_visible_row(std::size_t row) noexcept;
    void scroll_rows(int delta) noexcept;
    void scroll_to_fraction(double fraction) noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] std::size_t maximum_first_visible_row() const noexcept;
    [[nodiscard]] double scroll_fraction() const noexcept;

private:
    [[nodiscard]] bool visible(const ServerBrowserEntry& server) const noexcept;
    void rebuild_visible();
    void sort_visible();
    void preserve_or_clear_selection();
    void clamp_scroll() noexcept;

    std::vector<ServerBrowserEntry> servers_{};
    std::vector<std::size_t> visible_indices_{};
    std::optional<std::string> selected_identifier_{};
    std::function<std::string(std::string_view)> mode_title_lookup_{};
    ServerBrowserSource source_{ServerBrowserSource::all};
    /** config.py: server_region defaults to US West until the player picks one. */
    ServerBrowserRegion region_{ServerBrowserRegion::us_west};
    ServerSortColumn sort_column_{ServerSortColumn::ping};
    bool sort_descending_{};
    bool show_full_servers_{true};
    bool show_empty_servers_{true};
    std::size_t visible_row_capacity_{retail_visible_row_count};
    std::size_t first_visible_row_{};
    std::uint64_t refresh_generation_{};
    bool refreshing_{};
};

[[nodiscard]] std::string_view localization_key(ServerBrowserSource source) noexcept;
[[nodiscard]] std::string_view localization_key(ServerBrowserRegion region) noexcept;

namespace join_match_assets {

inline constexpr std::string_view three_button_frame{"png/ui/main_menu/frame_3button_menu.png"};
inline constexpr std::string_view small_navigation_frame{
    "png/ui/main_menu/frame_nav_bar_small.png"};
inline constexpr std::string_view back_icon{"png/ui/common_elements/nav_bar/back_icon.png"};
inline constexpr std::string_view server_content_frame{
    "png/ui/server select/server_select_content_frames.png"};
inline constexpr std::string_view server_tab_frame{"png/ui/server select/tab_name_frame.png"};
inline constexpr std::string_view favourite_star{"png/ui/server select/favourite_star.png"};
inline constexpr std::string_view favourite_star_button{
    "png/ui/server select/favourite_star_button.png"};
inline constexpr std::string_view map_placeholder{"png/ui/server select/map_placeholder.png"};
inline constexpr std::string_view sort_up{"png/ui/common_elements/filter_arrow_up.png"};
inline constexpr std::string_view sort_down{"png/ui/common_elements/filter_arrow_down.png"};
inline constexpr std::string_view scroll_up{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png"};
inline constexpr std::string_view scroll_down{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
inline constexpr std::string_view scroll_thumb_top{
    "png/ui/common_elements/scroll_bar/scroll_bar_top.png"};
inline constexpr std::string_view scroll_thumb_middle{
    "png/ui/common_elements/scroll_bar/scroll_bar_mid.png"};
inline constexpr std::string_view scroll_thumb_bottom{
    "png/ui/common_elements/scroll_bar/scroll_bar_bottom.png"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace join_match_assets

} // namespace battlespades::frontend
