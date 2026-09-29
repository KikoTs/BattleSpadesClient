#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/focus_navigator.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battlespades::frontend {

/** The two filters exposed by retail `UGCSquadsMenu`. */
enum class UgcEditorLobbySource : std::uint8_t {
    friends,
    open,
};

enum class UgcEditorDiscoveryState : std::uint8_t {
    offline,
    idle,
    refreshing,
    ready,
    error,
};

/** Immutable lobby data supplied by the future UGC matchmaking adapter. */
struct UgcEditorLobbyRecord final {
    std::string lobby_id;
    std::string name;
    std::uint16_t member_count{};
    std::uint16_t maximum_members{};
    std::uint16_t ping_milliseconds{};
    bool open{true};

    [[nodiscard]] bool full() const noexcept;
    [[nodiscard]] friend bool operator==(const UgcEditorLobbyRecord&,
                                         const UgcEditorLobbyRecord&) = default;
};

enum class UgcEditorBrowserIntentKind : std::uint8_t {
    refresh_lobbies,
    join_lobby,
    create_lobby,
    back_to_ugc_select,
};

struct UgcEditorBrowserIntent final {
    UgcEditorBrowserIntentKind kind{UgcEditorBrowserIntentKind::refresh_lobbies};
    UgcEditorLobbySource source{UgcEditorLobbySource::open};
    std::string lobby_id;

    [[nodiscard]] friend bool operator==(const UgcEditorBrowserIntent&,
                                         const UgcEditorBrowserIntent&) = default;
};

enum class UgcEditorBrowserControlKind : std::uint8_t {
    back,
    source_filter,
    join_lobby,
    new_lobby,
};

struct UgcEditorBrowserControl final {
    ui::Widget widget{};
    UgcEditorBrowserControlKind kind{UgcEditorBrowserControlKind::back};
    std::string_view localization_key;
};

/**
 * Renderer-neutral reconstruction of retail `UGCSquadsMenu`.
 *
 * Lobby discovery and lobby creation remain typed application intentions. The
 * local New Lobby control deliberately remains available while discovery is
 * offline, because the native client can host its bundled map-editor server
 * without pretending that Steam matchmaking exists.
 */
class UgcEditorBrowserModel final {
public:
    static constexpr std::size_t control_count{4U};
    static constexpr std::size_t visible_lobby_rows{13U};
    static constexpr std::size_t maximum_lobbies{4'096U};

    UgcEditorBrowserModel();

    [[nodiscard]] std::span<const UgcEditorBrowserControl> controls() const noexcept;
    [[nodiscard]] std::span<const UgcEditorLobbyRecord> lobbies() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] const UgcEditorLobbyRecord* selected_lobby() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] UgcEditorLobbySource source() const noexcept;
    [[nodiscard]] UgcEditorDiscoveryState discovery_state() const noexcept;
    [[nodiscard]] std::string_view status_localization_key() const noexcept;

    void set_network_available(bool available) noexcept;
    [[nodiscard]] std::optional<UgcEditorBrowserIntent> request_refresh() noexcept;
    [[nodiscard]] bool complete_refresh(std::vector<UgcEditorLobbyRecord> lobbies,
                                        bool success);
    [[nodiscard]] std::optional<UgcEditorBrowserIntent>
    set_source(UgcEditorLobbySource source) noexcept;
    [[nodiscard]] std::optional<UgcEditorBrowserIntent> cycle_source() noexcept;
    [[nodiscard]] bool select_lobby(std::size_t index) noexcept;
    [[nodiscard]] bool select_visible_row(std::size_t row) noexcept;
    void scroll_rows(int delta) noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<UgcEditorBrowserIntent>
    pointer_release(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<UgcEditorBrowserIntent>
    handle(ui::InputEvent event) noexcept;

    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> control_index(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hit_control(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<std::size_t> hit_lobby_row(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<UgcEditorBrowserIntent>
    activate(UgcEditorBrowserControlKind kind) noexcept;
    [[nodiscard]] bool valid_lobby(const UgcEditorLobbyRecord& lobby) const noexcept;
    void update_control_states() noexcept;
    void rebuild_focus() noexcept;
    void repair_selection() noexcept;

    std::array<UgcEditorBrowserControl, control_count> controls_{};
    std::vector<UgcEditorLobbyRecord> lobbies_{};
    std::optional<std::string> selected_lobby_id_{};
    std::size_t first_visible_row_{};
    UgcEditorLobbySource source_{UgcEditorLobbySource::open};
    UgcEditorDiscoveryState discovery_state_{UgcEditorDiscoveryState::offline};
    ui::FocusNavigator focus_{};
    std::optional<ui::WidgetId> hovered_{};
    std::optional<ui::WidgetId> pressed_control_{};
    std::optional<std::string> pressed_lobby_id_{};
    bool pointer_down_{};
};

[[nodiscard]] std::string_view localization_key(UgcEditorLobbySource source) noexcept;

enum class UgcEditorPrivacy : std::uint8_t {
    invite_only,
    friends_only,
    open,
};

enum class UgcEditorSettingId : std::uint8_t {
    privacy,
    maximum_players,
    map,
    prefab_set,
    ugc_mode,
    map_title,
};

struct UgcEditorSettingDefinition final {
    UgcEditorSettingId id{UgcEditorSettingId::privacy};
    std::string_view label_key;
    bool editable_text{};
};

/**
 * One reopenable hosted_ugc project listed under the lobby Map row's
 * SAVED_MAPS category (retail mapsPanel.populate_playlist, ugc_mode branch).
 */
struct UgcEditorSavedProject final {
    /** File stem shared by the .vxl/.txt/.ugc triplet, e.g. "Custommap_3". */
    std::string stem;
    std::string title;
    /** One of ugc_editor_maps(); the project's fixed baseplate. */
    std::string baseplate;
    std::string author;
    std::optional<std::uint8_t> prefab_set{};

    [[nodiscard]] friend bool operator==(const UgcEditorSavedProject&,
                                         const UgcEditorSavedProject&) = default;
};

/** Exact UGC settings persisted into the editor-server launch request. */
struct UgcEditorConfiguration final {
    UgcEditorPrivacy privacy{UgcEditorPrivacy::open};
    std::uint8_t maximum_players{12U};
    /** Baseplate stem (template or the saved project's own baseplate). */
    std::string map_name{"DesertBaseplate"};
    std::string ugc_mode{"tdm"};
    std::uint8_t prefab_set{1U};
    /** generate_ugc_map_title(TEMPLATE title): "Desert", "Desert-1", ... */
    std::string map_title{"Desert"};
    /**
     * Project identity (file stem) passed to the editor server. Templates
     * get a fresh generate_ugc_map_filename "Custommap_N"; SAVED_MAPS keep
     * the chosen project's stem so reopening never clobbers another map.
     */
    std::string project_stem{"Custommap_1"};
    /** True when map_name/project_stem name an existing SAVED_MAPS project. */
    bool saved_project{};

    [[nodiscard]] friend bool operator==(const UgcEditorConfiguration&,
                                         const UgcEditorConfiguration&) = default;
};

enum class UgcEditorLobbyIntentKind : std::uint8_t {
    leave_lobby,
    invite_friends,
    start_editor,
};

struct UgcEditorLobbyIntent final {
    UgcEditorLobbyIntentKind kind{UgcEditorLobbyIntentKind::leave_lobby};
    UgcEditorConfiguration configuration{};

    [[nodiscard]] friend bool operator==(const UgcEditorLobbyIntent&,
                                         const UgcEditorLobbyIntent&) = default;
};

enum class UgcEditorLobbyControlKind : std::uint8_t {
    back,
    invite,
    start,
    setting,
};

struct UgcEditorLobbyControl final {
    ui::Widget widget{};
    UgcEditorLobbyControlKind kind{UgcEditorLobbyControlKind::back};
    std::optional<UgcEditorSettingId> setting;
    std::string_view localization_key;
};

/**
 * Host-side reconstruction of `UGCSquadLobbyMenu` and its MatchSettingsPanel.
 *
 * The six retail UGC settings are one atomic configuration. Start emits this
 * snapshot; only the application layer may launch a server or enter Loading.
 */
class UgcEditorLobbyModel final {
public:
    static constexpr std::size_t setting_count{6U};
    static constexpr std::size_t control_count{9U};
    static constexpr std::size_t maximum_title_code_units{19U};

    UgcEditorLobbyModel();
    void set_host_authority(bool host) noexcept;
    /**
     * Installs the SAVED_MAPS catalog (listed before the nine TEMPLATES on
     * the Map row) and every file stem already present in hosted_ugc/maps.
     * An untouched template selection re-derives its unique title/stem.
     */
    void set_saved_projects(std::vector<UgcEditorSavedProject> projects,
                            std::vector<std::string> taken_stems = {});
    /** Localised TEMPLATE names (strings DesertBaseplate..); English by default. */
    void set_template_titles(std::span<const std::string> titles);
    [[nodiscard]] std::span<const UgcEditorSavedProject> saved_projects() const noexcept;
    [[nodiscard]] bool apply_configuration(const UgcEditorConfiguration& configuration);
    void set_members(std::vector<std::pair<std::string, bool>> members);
    [[nodiscard]] const std::vector<std::pair<std::string, bool>>& members() const noexcept;

    [[nodiscard]] const UgcEditorConfiguration& configuration() const noexcept;
    [[nodiscard]] std::span<const UgcEditorLobbyControl> controls() const noexcept;
    [[nodiscard]] std::span<const UgcEditorSettingDefinition> settings() const noexcept;
    [[nodiscard]] std::string value_text(UgcEditorSettingId setting) const;
    [[nodiscard]] bool title_editing() const noexcept;

    [[nodiscard]] bool cycle(UgcEditorSettingId setting, int direction) noexcept;
    [[nodiscard]] bool begin_title_edit() noexcept;
    [[nodiscard]] bool append_title_text(std::string_view ascii_text);
    [[nodiscard]] bool erase_title_character() noexcept;
    [[nodiscard]] bool commit_title_edit() noexcept;
    void cancel_title_edit() noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<UgcEditorLobbyIntent>
    pointer_release(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<UgcEditorLobbyIntent> handle(ui::InputEvent event) noexcept;

    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> control_index(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hit_control(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<UgcEditorLobbyIntent>
    activate(const UgcEditorLobbyControl& control) noexcept;
    [[nodiscard]] std::optional<UgcEditorSettingId> focused_setting() const noexcept;
    void rebuild_focus() noexcept;
    void select_template(std::size_t index);
    void select_saved_project(std::size_t index);

    UgcEditorConfiguration configuration_{};
    std::vector<UgcEditorSavedProject> saved_projects_{};
    std::vector<std::string> taken_stems_{};
    std::array<std::string, 9U> template_titles_{};
    bool title_customized_{};
    bool host_authority_{true};
    std::vector<std::pair<std::string, bool>> members_{};
    std::array<UgcEditorSettingDefinition, setting_count> settings_{};
    std::array<UgcEditorLobbyControl, control_count> controls_{};
    ui::FocusNavigator focus_{};
    std::optional<ui::WidgetId> hovered_{};
    std::optional<ui::WidgetId> pressed_control_{};
    int pressed_direction_{};
    bool pointer_down_{};
    bool title_editing_{};
    std::string title_before_edit_{};
};

[[nodiscard]] std::span<const std::string_view> ugc_editor_maps() noexcept;
/** String id naming each ugc_editor_maps() entry (strings.DesertBaseplate...). */
[[nodiscard]] std::span<const std::string_view> ugc_editor_template_title_keys() noexcept;
/**
 * Canonical ugc_editor_maps() entry for a sidecar `baseplate` stem, matched
 * case-insensitively (TempleBaseplate -> Templebaseplate, MarshBaseplate ->
 * MarshTemplate). Empty when the stem is not one of the nine baseplates.
 */
[[nodiscard]] std::string_view ugc_editor_map_for_baseplate(std::string_view baseplate) noexcept;
/** Shared arrow geometry for pointer input and rendering, in UI subpixels. */
[[nodiscard]] ui::Rect ugc_editor_setting_arrow(ui::Rect row, int direction) noexcept;
[[nodiscard]] std::span<const std::string_view> ugc_editor_modes() noexcept;
[[nodiscard]] std::span<const std::string_view> ugc_editor_mode_labels() noexcept;
[[nodiscard]] std::span<const std::string_view> ugc_editor_prefab_labels() noexcept;

/** Rows in retail's host-only in-game `UGCSettings` overlay. */
enum class UgcIngameSettingField : std::uint8_t {
    skydome,
    water_red,
    water_green,
    water_blue,
    target_mode,
    map_title,
    map_preview,
};

/** Atomic values submitted by APPLY; Cancel restores the opening snapshot. */
struct UgcIngameSettingsDraft final {
    std::string skydome{"User_Grassland.txt"};
    std::array<std::uint8_t, 4U> water{18U, 67U, 94U, 255U};
    std::uint8_t target_mode{6U};
    std::string map_title{"Untitled Map"};
    [[nodiscard]] friend bool operator==(const UgcIngameSettingsDraft&,
                                         const UgcIngameSettingsDraft&) = default;
};

/** Renderer-neutral state for the recovered host-only in-game settings. */
class UgcIngameSettingsModel final {
public:
    static constexpr std::size_t field_count{7U};
    static constexpr std::size_t maximum_title_code_units{19U};

    [[nodiscard]] bool open(bool host,
                            UgcIngameSettingsDraft current,
                            std::vector<std::string> valid_skydomes);
    void cancel() noexcept;
    [[nodiscard]] std::optional<UgcIngameSettingsDraft> apply() noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool title_editing() const noexcept;
    [[nodiscard]] UgcIngameSettingField focused_field() const noexcept;
    [[nodiscard]] const UgcIngameSettingsDraft& draft() const noexcept;
    [[nodiscard]] std::span<const std::string> valid_skydomes() const noexcept;

    /** Selects one retail settings row. Ignored while hidden or editing text. */
    [[nodiscard]] bool set_focused_field(UgcIngameSettingField field) noexcept;
    void move_focus(int direction) noexcept;
    [[nodiscard]] bool adjust_focused(int direction) noexcept;
    [[nodiscard]] bool begin_title_edit() noexcept;
    [[nodiscard]] bool append_title_text(std::string_view ascii_text);
    [[nodiscard]] bool erase_title_character() noexcept;
    void finish_title_edit() noexcept;

private:
    UgcIngameSettingsDraft opening_{};
    UgcIngameSettingsDraft draft_{};
    std::vector<std::string> skydomes_{};
    std::size_t focused_{};
    bool visible_{};
    bool title_editing_{};
};

[[nodiscard]] std::string_view ugc_ingame_mode_name(std::uint8_t mode) noexcept;

} // namespace battlespades::frontend
