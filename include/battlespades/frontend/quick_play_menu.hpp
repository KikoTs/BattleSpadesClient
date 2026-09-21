#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace battlespades::frontend {

/** The fixed public-server mode passed by retail QuickPlayMenu. */
enum class QuickPlayServerMode : std::uint8_t {
    public_match,
};

/** Lifecycle of the external public-server discovery request. */
enum class QuickPlaySearchState : std::uint8_t {
    idle,
    searching,
    complete,
    failed,
    unavailable,
};

/** One rule/value pair from a retail playlist file. */
struct QuickPlayRule final {
    std::string_view localization_key;
    std::string_view value;

    [[nodiscard]] friend constexpr bool operator==(const QuickPlayRule&,
                                                   const QuickPlayRule&) = default;
};

/** Immutable data reconstructed from the shipped playlist text files. */
struct QuickPlayPlaylistDefinition final {
    std::uint32_t id{};
    std::string_view name_key;
    std::span<const std::string_view> modes;
    std::span<const std::string_view> maps;
    std::span<const QuickPlayRule> rules;
    bool classic{};
    bool mafia_content{};
};

/** Public-master response before it is admitted to a playlist. */
struct QuickPlayServerResponse final {
    std::uint32_t playlist_id{};
    std::string name;
    std::string address;
    std::uint16_t game_port{};
    std::uint16_t query_port{};
    double ping_seconds{};
    std::string map;
    std::string mode;
    std::string mode_id;
    std::uint16_t players{};
    std::uint16_t maximum_players{};
    std::string tags;
    std::string texture_skin;
    bool classic{};
    bool matching_version{true};
    std::string identity_server_id;
    bool identity_ticket{};

    [[nodiscard]] std::string identifier() const;
};

/** Mutable row state shown by the ranked-playlist list panel. */
struct QuickPlayPlaylistRow final {
    const QuickPlayPlaylistDefinition* definition{};
    bool owned{true};
    double lowest_ping_seconds{1'000.0};
    std::vector<QuickPlayServerResponse> server_responses;
    std::optional<std::size_t> chosen_server_index;
    std::optional<std::uint32_t> displayed_ping_milliseconds;
    std::string displayed_players;

    [[nodiscard]] const QuickPlayServerResponse* chosen_server() const noexcept;
};

/** Adapter request corresponding to SteamGetInternetServerList(PUBLIC). */
struct QuickPlaySearchIntent final {
    std::uint64_t generation{};
    QuickPlayServerMode server_mode{QuickPlayServerMode::public_match};

    [[nodiscard]] friend constexpr bool operator==(const QuickPlaySearchIntent&,
                                                   const QuickPlaySearchIntent&) = default;
};

/** LoadingMenu handoff when Quick Play has already selected a concrete server. */
struct QuickPlayDirectStartIntent final {
    std::string identifier;
    QuickPlayServerMode server_mode{QuickPlayServerMode::public_match};
    std::string server_name;
    std::string expected_map;
    std::string expected_mode;
    std::string expected_skin;
    bool expected_classic{};
    std::string identity_server_id;
    bool identity_ticket{};

    [[nodiscard]] friend bool operator==(const QuickPlayDirectStartIntent&,
                                         const QuickPlayDirectStartIntent&) = default;
};

/** JoiningGameMenu handoff when the selected playlist has no concrete server. */
struct QuickPlayPlaylistStartIntent final {
    QuickPlayServerMode server_mode{QuickPlayServerMode::public_match};
    std::uint32_t playlist_id{};

    [[nodiscard]] friend constexpr bool operator==(const QuickPlayPlaylistStartIntent&,
                                                   const QuickPlayPlaylistStartIntent&) = default;
};

/** Returns to Join Match using the shared reverse slide. */
struct QuickPlayBackIntent final {
    [[nodiscard]] friend constexpr bool operator==(QuickPlayBackIntent,
                                                   QuickPlayBackIntent) = default;
};

/** Opens the recovered Mafia/DLC store page instead of starting an unowned row. */
struct QuickPlayBuyIntent final {
    std::string_view content_key{"mafia"};
    std::uint32_t application_id{420'650U};

    [[nodiscard]] friend constexpr bool operator==(QuickPlayBuyIntent,
                                                   QuickPlayBuyIntent) = default;
};

using QuickPlayIntent =
    std::variant<QuickPlaySearchIntent,
                 QuickPlayDirectStartIntent,
                 QuickPlayPlaylistStartIntent,
                 QuickPlayBackIntent,
                 QuickPlayBuyIntent>;

enum class QuickPlayFixedControl : std::uint8_t {
    refresh,
    primary,
    back,
};

enum class QuickPlayPrimaryKind : std::uint8_t {
    start,
    buy,
    hidden,
};

/**
 * Renderer- and transport-neutral reconstruction of retail `QuickPlayMenu`.
 *
 * The model owns no Steam or socket object. An adapter explicitly enables
 * discovery, consumes `begin_search()`, and feeds responses back with the
 * same generation. Late callbacks fail closed. With no adapter attached the
 * model remains in `unavailable`: Start and Refresh cannot leak a connection
 * attempt, while playlist inspection, DLC purchase, and Back stay functional.
 */
class QuickPlayMenuModel final {
public:
    static constexpr std::size_t visible_playlist_rows{11U};

    explicit QuickPlayMenuModel(bool mafia_content_owned = true,
                                std::uint32_t random_seed = 0xA05'2012U);

    [[nodiscard]] std::span<const QuickPlayPlaylistRow> rows() const noexcept;
    [[nodiscard]] std::size_t selected_row() const noexcept;
    [[nodiscard]] const QuickPlayPlaylistRow& selected() const noexcept;
    [[nodiscard]] bool select_row(std::size_t row) noexcept;

    void set_mafia_content_owned(bool owned) noexcept;
    void set_network_available(bool available) noexcept;
    [[nodiscard]] bool network_available() const noexcept;
    [[nodiscard]] QuickPlaySearchState search_state() const noexcept;
    [[nodiscard]] std::optional<QuickPlaySearchIntent> begin_search() noexcept;
    [[nodiscard]] bool accept_server(QuickPlaySearchIntent request,
                                     QuickPlayServerResponse response);
    /** Admit an unassigned public-list response to its compatible retail playlists. */
    [[nodiscard]] std::size_t accept_public_server(QuickPlaySearchIntent request,
                                                  const QuickPlayServerResponse& response);
    [[nodiscard]] bool finish_search(QuickPlaySearchIntent request) noexcept;
    [[nodiscard]] bool fail_search(QuickPlaySearchIntent request) noexcept;

    [[nodiscard]] bool refresh_enabled() const noexcept;
    [[nodiscard]] QuickPlayPrimaryKind primary_kind() const noexcept;
    [[nodiscard]] bool primary_enabled() const noexcept;
    [[nodiscard]] std::optional<QuickPlayIntent> activate_primary() const;
    [[nodiscard]] QuickPlayBackIntent back() const noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<QuickPlayIntent>
    pointer_release(std::optional<ui::Point> point);
    [[nodiscard]] std::optional<QuickPlayIntent> handle(ui::InputEvent event);
    [[nodiscard]] std::optional<std::size_t> hovered_row() const noexcept;
    [[nodiscard]] std::optional<QuickPlayFixedControl> hovered_control() const noexcept;
    [[nodiscard]] WidgetVisualState visual_state(QuickPlayFixedControl control) const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> row_hit_test(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<QuickPlayFixedControl>
    control_hit_test(ui::Point point) const noexcept;
    [[nodiscard]] bool control_enabled(QuickPlayFixedControl control) const noexcept;
    [[nodiscard]] bool control_visible(QuickPlayFixedControl control) const noexcept;
    [[nodiscard]] bool request_is_current(QuickPlaySearchIntent request) const noexcept;
    void clear_discovery() noexcept;
    void choose_server(QuickPlayPlaylistRow& row);

    std::vector<QuickPlayPlaylistRow> rows_;
    std::size_t selected_row_{};
    bool mafia_content_owned_{true};
    bool network_available_{};
    QuickPlaySearchState search_state_{QuickPlaySearchState::unavailable};
    std::optional<QuickPlaySearchIntent> active_search_;
    std::uint64_t next_generation_{1U};
    std::mt19937 random_;
    std::optional<std::size_t> hovered_row_;
    std::optional<QuickPlayFixedControl> hovered_control_;
    bool pointer_down_{};
    bool refresh_armed_{};
    bool primary_armed_{};
    bool back_armed_{};
};

[[nodiscard]] std::span<const QuickPlayPlaylistDefinition>
quick_play_playlist_definitions() noexcept;

} // namespace battlespades::frontend
