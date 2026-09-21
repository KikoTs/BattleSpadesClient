#pragma once

#include "battlespades/ui/geometry.hpp"
#include "battlespades/ui/input.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace battlespades::frontend {

/** Retail Match Lobby panels reachable from the Create Match main-menu action. */
enum class CreateMatchPage : std::uint8_t {
    match_settings,
    choose_game_mode,
    choose_map,
    game_rules,
};

/** Lobby-level actions; local hosting explicitly bypasses public relay allocation. */
enum class CreateMatchRouteAction : std::uint8_t {
    leave_lobby,
    start_game,
    start_local_game,
    join_game,
};

/** Platform-owned actions which never mutate the local lobby draft directly. */
enum class CreateMatchPlatformAction : std::uint8_t {
    invite_friends,
};

/** Semantic navigation direction consumed by the shared menu transition animator. */
enum class CreateMatchNavigationDirection : std::uint8_t {
    forward,
    backward,
};

enum class CreateMatchPrivacy : std::uint8_t {
    invite_only,
    friends_only,
    open,
};

enum class CreateMatchModeFamily : std::uint8_t {
    standard,
    classic,
    mafia,
};

struct CreateMatchModeDefinition final {
    std::uint16_t retail_playlist_id{};
    std::string_view mode_key{};
    std::string_view label_key{};
    CreateMatchModeFamily family{CreateMatchModeFamily::standard};
    std::span<const std::string_view> maps{};
    std::uint16_t default_players{12U};
    std::uint16_t default_match_minutes{15U};
};

struct CreateMatchRuleDefinition final {
    std::string_view category_key{};
    std::string_view rule_key{};
    std::string_view default_value{};
    std::span<const std::string_view> values{};
    bool available_standard{true};
    bool available_classic{true};
    bool available_mafia{true};
    /** Empty for rules that do not depend on an enabled standard class. */
    std::span<const std::string_view> enabling_class_rules{};
};

/** Immutable retail data recovered from playlist text files and constants_matchmaking.py. */
[[nodiscard]] std::span<const CreateMatchModeDefinition> retail_create_match_modes() noexcept;
[[nodiscard]] std::span<const CreateMatchRuleDefinition> retail_create_match_rules() noexcept;

struct CreateMatchConfiguration final {
    CreateMatchPrivacy privacy{CreateMatchPrivacy::open};
    std::uint16_t retail_playlist_id{9U};
    std::uint16_t max_players{12U};
    std::uint16_t match_minutes{15U};
    /** Client extension: server-owned participants for this private match. */
    std::uint16_t bot_count{};
    std::string bot_difficulty{"mixed"};
    /** Preferred port; the launcher advances to the next free UDP port. */
    std::uint16_t server_port{27015U};
    std::string map_name{"AncientEgypt"};
    /** Only explicit deviations from the selected playlist defaults are stored. */
    std::map<std::string, std::string, std::less<>> rule_overrides{};

    [[nodiscard]] friend bool operator==(const CreateMatchConfiguration&,
                                         const CreateMatchConfiguration&) = default;
};

struct CreateMatchPlayer final {
    std::uint64_t account_id{};
    std::string display_name{};
    std::string team_key{"TEAM_NEUTRAL"};
    bool host{};
    bool local{};
    bool in_game{};

    [[nodiscard]] friend bool operator==(const CreateMatchPlayer&,
                                         const CreateMatchPlayer&) = default;
};

struct CreateMatchChatLine final {
    std::string author;
    std::string message;

    [[nodiscard]] friend bool operator==(const CreateMatchChatLine&,
                                         const CreateMatchChatLine&) = default;
};

enum class CreateMatchRowKind : std::uint8_t {
    menu_link,
    stepped_choice,
    selectable_item,
    category,
    toggle,
};

enum class CreateMatchVisualState : std::uint8_t {
    normal,
    hovered,
    focused,
    disabled,
};

struct CreateMatchRowPresentation final {
    /** Stable settings id, mode key, map name, category key, or rule id. */
    std::string stable_key{};
    std::string label_key{};
    std::string value_text{};
    CreateMatchRowKind kind{CreateMatchRowKind::menu_link};
    ui::Rect bounds{};
    ui::Rect control_bounds{};
    bool enabled{true};
    bool selected{};
    bool expanded{};
    std::size_t value_index{};
    std::size_t value_count{};
    CreateMatchVisualState visual_state{CreateMatchVisualState::normal};
};

struct CreateMatchButtonPresentation final {
    std::string stable_key{};
    std::string label_key{};
    ui::Rect bounds{};
    bool enabled{true};
    CreateMatchVisualState visual_state{CreateMatchVisualState::normal};
};

/** Complete top-left-origin 800x600 state for a Match Lobby frame. */
struct CreateMatchMenuPresentation final {
    static constexpr std::int32_t reference_width{800};
    static constexpr std::int32_t reference_height{600};

    CreateMatchPage page{CreateMatchPage::match_settings};
    std::string_view title_key{"MATCH_LOBBY"};
    std::string_view panel_title_key{"MATCH_SETTINGS"};
    // Exact top-left conversions of ListPreviewMenuBase/BaseSquadLobbyMenu's
    // bottom-left 800x600 coordinates.
    ui::Rect outer_frame{25, 5, 750, 589};
    ui::Rect title_bounds{120, 25, 560, 50};
    ui::Rect player_panel{56, 95, 340, 270};
    ui::Rect content_panel{400, 95, 340, 355};
    ui::Rect player_header{66, 105, 320, 40};
    ui::Rect content_header{410, 105, 320, 40};
    ui::Rect lobby_name_field{76, 111, 210, 30};
    ui::Rect player_count_bar{56, 367, 340, 20};
    ui::Rect chat_panel{56, 390, 340, 116};
    ui::Rect chat_input{63, 477, 325, 24};
    ui::Rect action_panel{401, 452, 340, 54};
    ui::Rect defaults_help{492, 415, 238, 28};
    ui::Rect scrollbar_bounds{708, 155, 22, 285};
    ui::Rect navigation_bar{54, 541, 695, 32};
    std::string lobby_name{};
    std::vector<CreateMatchPlayer> players{};
    std::size_t first_visible_player{};
    bool member_management{};
    std::vector<CreateMatchChatLine> chat_lines{};
    std::string chat_draft{};
    bool chat_focused{};
    std::vector<CreateMatchRowPresentation> rows{};
    std::vector<CreateMatchButtonPresentation> buttons{};
    std::size_t first_visible_row{};
    std::size_t maximum_scroll{};
    bool show_scrollbar{};
    bool show_content_frame{};
    bool show_defaults_help{};
    bool dirty{};
    std::optional<std::string> focused_key{};
    std::optional<std::string> hovered_key{};
};

enum class CreateMatchSound : std::uint8_t {
    confirm,
    back,
    scroll,
};

struct CreateMatchSoundEffect final {
    CreateMatchSound sound{CreateMatchSound::confirm};
};

struct CreateMatchPageChangedEffect final {
    CreateMatchPage from{CreateMatchPage::match_settings};
    CreateMatchPage to{CreateMatchPage::match_settings};
    CreateMatchNavigationDirection direction{CreateMatchNavigationDirection::forward};
};

struct CreateMatchConfigurationChangedEffect final {
    CreateMatchConfiguration configuration{};
};

struct CreateMatchRouteEffect final {
    CreateMatchRouteAction action{CreateMatchRouteAction::leave_lobby};
    CreateMatchConfiguration configuration{};
};

struct CreateMatchPlatformActionEffect final {
    CreateMatchPlatformAction action{CreateMatchPlatformAction::invite_friends};
};

struct CreateMatchChatEffect final {
    std::string message;
};

struct CreateMatchMemberEffect final {
    std::uint64_t account_id{};
    bool kick{};
    std::uint8_t team{}; // 0 = automatic; 2/3 = retail team IDs.
};

using CreateMatchEffect = std::variant<CreateMatchSoundEffect,
                                       CreateMatchPageChangedEffect,
                                       CreateMatchConfigurationChangedEffect,
                                       CreateMatchRouteEffect,
                                       CreateMatchPlatformActionEffect,
                                       CreateMatchChatEffect,
                                       CreateMatchMemberEffect>;

/**
 * Renderer- and Steam-neutral reconstruction of the retail Create Match lobby.
 *
 * Retail mutates lobby metadata immediately. This model mirrors that behavior
 * in a local draft and emits an immutable snapshot after each mutation. The
 * platform layer may publish the snapshot to a local server or online lobby.
 */
class CreateMatchMenuModel final {
public:
    explicit CreateMatchMenuModel(CreateMatchConfiguration initial = {});

    [[nodiscard]] CreateMatchPage page() const noexcept;
    [[nodiscard]] const CreateMatchConfiguration& configuration() const noexcept;
    [[nodiscard]] const std::string& lobby_name() const noexcept;
    [[nodiscard]] bool host_authority() const noexcept;
    [[nodiscard]] CreateMatchMenuPresentation presentation() const;

    void set_players(std::vector<CreateMatchPlayer> players);
    void set_chat_lines(std::vector<CreateMatchChatLine> lines);
    /** Apply one server-authoritative lobby snapshot without emitting edits. */
    void apply_authoritative_configuration(CreateMatchConfiguration configuration);
    /** Only the authoritative owner may mutate settings or start the match. */
    void set_host_authority(bool host) noexcept;
    /** An authoritative ready/in-game lobby may be joined without hosting again. */
    void set_match_join_available(bool available, bool busy = false) noexcept;
    void set_member_management_enabled(bool enabled) noexcept;
    [[nodiscard]] bool request_member_action(std::uint64_t account_id, bool kick);
    /** Retail lobby names are non-empty and limited to 19 Unicode code points. */
    [[nodiscard]] bool set_lobby_name(std::string value);
    [[nodiscard]] bool append_chat_text(std::string_view utf8);
    [[nodiscard]] bool erase_chat_code_point() noexcept;
    [[nodiscard]] bool submit_chat();
    void cancel_chat() noexcept;
    [[nodiscard]] bool chat_focused() const noexcept;

    [[nodiscard]] bool open_page(CreateMatchPage page);
    [[nodiscard]] bool activate_back();
    [[nodiscard]] bool activate_done();
    [[nodiscard]] bool activate_local_game();
    void activate_defaults();

    [[nodiscard]] bool set_focus(std::string_view stable_key);
    [[nodiscard]] bool activate_focused();
    [[nodiscard]] bool activate_row(std::string_view stable_key);
    [[nodiscard]] bool adjust_row(std::string_view stable_key, std::int32_t direction);
    [[nodiscard]] bool set_rule_value(std::string_view rule_key, std::string_view value);
    [[nodiscard]] bool set_category_expanded(std::string_view category_key, bool expanded);
    [[nodiscard]] bool set_scroll(std::size_t first_visible_row);
    [[nodiscard]] bool mouse_wheel(std::int32_t vertical_steps);
    [[nodiscard]] bool handle(ui::InputEvent event);

    void pointer_move(std::optional<ui::Point> point);
    void pointer_press(std::optional<ui::Point> point);
    void pointer_release(ui::Point point);

    [[nodiscard]] std::vector<CreateMatchEffect> take_effects() noexcept;

private:
    [[nodiscard]] const CreateMatchModeDefinition& selected_mode() const noexcept;
    [[nodiscard]] std::string resolved_rule_value(const CreateMatchRuleDefinition& rule) const;
    [[nodiscard]] std::string playlist_default_value(const CreateMatchRuleDefinition& rule) const;
    [[nodiscard]] bool rule_available(const CreateMatchRuleDefinition& rule) const;
    [[nodiscard]] bool rule_enabled(const CreateMatchRuleDefinition& rule) const;
    [[nodiscard]] std::vector<std::string> expanded_row_keys() const;
    [[nodiscard]] std::optional<CreateMatchRowPresentation>
    row_with_key(std::string_view stable_key) const;
    [[nodiscard]] std::size_t visible_capacity() const noexcept;
    [[nodiscard]] std::size_t maximum_scroll() const;
    void clamp_scroll();
    void reveal_focus();
    void repair_focus(bool reveal = true);
    void scroll_from_pointer(ui::Point point);
    void emit_configuration_changed();
    void select_mode(const CreateMatchModeDefinition& mode);

    CreateMatchConfiguration configuration_{};
    CreateMatchConfiguration retail_defaults_{};
    CreateMatchPage page_{CreateMatchPage::match_settings};
    CreateMatchPage previous_page_{CreateMatchPage::match_settings};
    std::string lobby_name_{"Private Match"};
    std::vector<CreateMatchPlayer> players_{};
    std::vector<CreateMatchChatLine> chat_lines_{};
    std::string chat_draft_{};
    std::map<std::string, bool, std::less<>> expanded_categories_{};
    std::size_t first_visible_row_{};
    std::size_t first_visible_player_{};
    bool member_management_enabled_{};
    std::optional<std::string> pressed_member_key_{};
    std::optional<std::string> focused_key_{};
    std::optional<std::string> hovered_key_{};
    bool scrollbar_dragging_{};
    bool host_authority_{true};
    bool match_join_available_{};
    bool match_join_busy_{};
    bool chat_focused_{};
    std::vector<CreateMatchEffect> effects_{};
};

[[nodiscard]] std::string_view create_match_page_title(CreateMatchPage page) noexcept;

} // namespace battlespades::frontend
