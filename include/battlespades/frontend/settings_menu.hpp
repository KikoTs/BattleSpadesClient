#pragma once

#include "battlespades/settings/retail_key_names.hpp"
#include "battlespades/settings/settings_session.hpp"
#include "battlespades/ui/geometry.hpp"
#include "battlespades/ui/input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace battlespades::frontend {

/** Stable identities for every row recovered from the retail Settings menu. */
enum class SettingsRowId : std::uint8_t {
    language,
    master_volume,
    music_volume,
    fallback_music,
    ragdoll_corpses,
    blood_marks,
    invert_mouse,
    favorite_server,
    show_skins,
    show_other_skins,
    weapon_motion,
    ability_hints,
    window_mode,
    resolution,
    graphics_api,
    antialiasing,
    effect_quality,
    draw_distance,
    shader_quality,
    texture_quality,
    model_quality,
    vsync,
    compatibility_shader,
    // Native Graphics rows, grouped under four category headers.
    graphics_display_category,
    graphics_quality_category,
    graphics_effects_category,
    graphics_color_category,
    graphics_preset,
    frame_limit,
    field_of_view,
    render_scale,
    upscale,
    sharpness,
    low_latency,
    show_fps,
    shadow_quality,
    shadow_distance,
    ambient_occlusion,
    anisotropic_filtering,
    texture_filtering,
    bloom,
    motion_blur,
    brightness,
    gamma,
    color_vision,
    main_controls_category,
    mouse_sensitivity,
    forward,
    backward,
    move_left,
    move_right,
    sneak,
    crouch,
    sprint,
    jump,
    fire_use,
    aim,
    reload,
    cycle_next_weapon,
    inventory_slots,
    team_chat,
    global_chat,
    show_map,
    view_scores,
    change_team,
    change_class,
    in_game_menu,
    pick_colour,
    map_vote_1,
    map_vote_2,
    map_vote_3,
    kick_player,
    toggle_hud,
    ugc_controls_category,
    ugc_settings,
    tool_help,
    palette_left,
    palette_right,
    palette_up,
    palette_down,
    cancel_prefab_placement,
    carve_prefab,
    jetpack_hover,
    quick_save,
};

enum class SettingsRowKind : std::uint8_t {
    category,
    continuous_slider,
    stepped_slider,
    toggle,
    choice,
    binding,
    fixed_binding,
};

enum class SettingsMenuContext : std::uint8_t {
    frontend,
    in_game,
};

struct SettingsLanguageOption final {
    std::string locale;
    std::string native_name;
};

/** Runtime facts that are deliberately not persisted in ClientSettings. */
struct SettingsMenuEnvironment final {
    SettingsMenuContext context{SettingsMenuContext::frontend};
    bool multisampling_supported{true};
    /**
     * The running renderer can change its MSAA sample count in place. False
     * on Direct3D (see render::multisample_change_is_live): Antialiasing then
     * reads RESTART_REQUIRED and Done reports a restart.
     */
    bool multisampling_live{true};
    bool glsl_shader_quality_supported{true};
    bool favorite_server_available{false};
    bool favorite_server{false};
    std::string favorite_server_description{};
    std::vector<SettingsLanguageOption> languages{{"en", "English"}};
    std::vector<settings::Resolution> display_modes{};
    /**
     * What the running renderer's world post chain can do (render scale,
     * sharpening, brightness/gamma/colour vision need the chain itself). A
     * row the backend cannot run stays visible but disabled, reading
     * NOT_SUPPORTED_BACKEND.
     */
    bool post_chain_supported{true};
    bool ambient_occlusion_supported{true};
    bool motion_blur_supported{true};
    bool bloom_supported{true};
    bool edge_adaptive_upscale_supported{true};
    /** Backends compiled into this executable, in the order shown to players. */
    std::vector<settings::GraphicsApi> graphics_apis{
        settings::GraphicsApi::automatic,
        settings::GraphicsApi::direct3d11,
        settings::GraphicsApi::direct3d12,
        settings::GraphicsApi::vulkan,
        settings::GraphicsApi::opengl,
        settings::GraphicsApi::metal,
    };
};

enum class SettingsTargetKind : std::uint8_t {
    tab,
    row,
    defaults_button,
    cancel_button,
    done_button,
};

/** One keyboard-focus, hover, or pointer-press target. */
struct SettingsMenuTarget final {
    SettingsTargetKind kind{SettingsTargetKind::tab};
    settings::SettingsTab tab{settings::SettingsTab::main};
    SettingsRowId row{SettingsRowId::master_volume};

    [[nodiscard]] static constexpr SettingsMenuTarget
    for_tab(settings::SettingsTab value) noexcept {
        return {SettingsTargetKind::tab, value, SettingsRowId::master_volume};
    }

    [[nodiscard]] static constexpr SettingsMenuTarget for_row(SettingsRowId value) noexcept {
        return {SettingsTargetKind::row, settings::SettingsTab::main, value};
    }

    [[nodiscard]] static constexpr SettingsMenuTarget button(SettingsTargetKind value) noexcept {
        return {value, settings::SettingsTab::main, SettingsRowId::master_volume};
    }

    [[nodiscard]] friend constexpr bool operator==(const SettingsMenuTarget&,
                                                   const SettingsMenuTarget&) = default;
};

enum class SettingsVisualState : std::uint8_t {
    normal,
    hovered,
    pressed,
    focused,
    disabled,
};

struct SettingsTabPresentation final {
    settings::SettingsTab tab{settings::SettingsTab::main};
    std::string_view label_key{};
    ui::Rect bounds{};
    bool selected{false};
    SettingsVisualState state{SettingsVisualState::normal};
};

struct SettingsRowPresentation final {
    SettingsRowId id{SettingsRowId::master_volume};
    SettingsRowKind kind{SettingsRowKind::toggle};
    std::string_view label_key{};
    std::string value_text{};
    std::string description{};
    std::vector<std::string> choices{};
    ui::Rect bounds{};
    ui::Rect control_bounds{};
    bool visible{false};
    bool enabled{false};
    bool expanded{false};
    double scalar_value{};
    std::size_t choice_index{};
    std::size_t choice_count{};
    /** DropBoxControl state. Only Resolution uses this in retail Settings. */
    bool dropdown_open{};
    std::size_t dropdown_first_index{};
    std::size_t dropdown_visible_count{};
    std::optional<settings::ControlAction> control_action{};
    SettingsVisualState state{SettingsVisualState::normal};
    /**
     * ToggleOptionControl hover: the pointer is over the half that is NOT
     * selected, which retail paints TOGGLE_OPTION_HOVERED_COLOUR.
     */
    bool unselected_half_hovered{};
    /** EditBoxFloatControl focus: the sensitivity box is taking typed text. */
    bool text_editing{};
};

struct SettingsButtonPresentation final {
    SettingsTargetKind id{SettingsTargetKind::done_button};
    std::string_view label_key{};
    ui::Rect bounds{};
    bool enabled{true};
    SettingsVisualState state{SettingsVisualState::normal};
};

struct BindingCapturePresentation final {
    settings::ControlAction action{settings::ControlAction::forward};
    std::optional<settings::BindingAssignmentResult> rejection{};
    /** The input that was refused, for ERROR_CONTROL_ALREADY_BOUND's "{0}". */
    std::optional<settings::InputBinding> rejected_binding{};
};

/** Complete renderer-neutral state for one Settings-menu frame. */
struct SettingsMenuPresentation final {
    static constexpr std::int32_t reference_width{800};
    static constexpr std::int32_t reference_height{600};

    settings::SettingsTab active_tab{settings::SettingsTab::main};
    std::array<SettingsTabPresentation, 3U> tabs{};
    std::vector<SettingsRowPresentation> rows{};
    std::array<SettingsButtonPresentation, 3U> buttons{};
    ui::Rect panel_bounds{};
    ui::Rect viewport_bounds{};
    std::size_t scroll_index{};
    std::size_t maximum_scroll_index{};
    bool in_game{false};
    bool dirty{false};
    std::string_view tooltip_key{};
    std::optional<SettingsMenuTarget> focused{};
    std::optional<SettingsMenuTarget> hovered{};
    std::optional<BindingCapturePresentation> binding_capture{};
};

enum class SettingsMenuSound : std::uint8_t {
    confirm,
    back,
    scroll,
};

struct SettingsSoundEffect final {
    SettingsMenuSound sound{SettingsMenuSound::confirm};
};

/** Live preview request emitted after a draft field changes. */
struct SettingsPreviewEffect final {
    SettingsRowId source{SettingsRowId::master_volume};
    settings::ClientSettings draft{};
};

struct SettingsDefaultsCommand final {
    settings::SettingsTab tab{settings::SettingsTab::main};
    settings::ClientSettings draft{};
};

/** Atomic persistence/runtime command emitted by Done. */
struct SettingsCommitCommand final {
    settings::ClientSettings settings{};
    bool changed{false};
    /** Window mode or resolution changed: applied with the keep/revert prompt. */
    bool display_changed{false};
    bool restart_required{false};
};

/** Runtime rollback request emitted by Cancel after previews may have run. */
struct SettingsRestoreCommand final {
    settings::ClientSettings settings{};
};

/** Transient Steam/server-browser favourite state, committed separately. */
struct SettingsFavoriteServerCommand final {
    bool favorite{false};
};

struct SettingsCloseCommand final {
    bool committed{false};
    /**
     * In-game only. Retail settingsMenu.save_pressed (Done) and the Menu key
     * leave straight to the game; back_pressed (Cancel) reopens EscapeMenu.
     */
    bool return_to_game{false};
};

struct SettingsBindingRejectedEffect final {
    settings::ControlAction action{settings::ControlAction::forward};
    settings::BindingAssignmentStatus status{settings::BindingAssignmentStatus::invalid_binding};
    std::optional<settings::ControlAction> conflicting_action{};
};

using SettingsMenuEffect = std::variant<SettingsSoundEffect,
                                        SettingsPreviewEffect,
                                        SettingsDefaultsCommand,
                                        SettingsCommitCommand,
                                        SettingsRestoreCommand,
                                        SettingsFavoriteServerCommand,
                                        SettingsCloseCommand,
                                        SettingsBindingRejectedEffect>;

/**
 * Renderer-neutral reconstruction of the retail three-tab Settings menu.
 *
 * Coordinates use the client's top-left 800x600 design canvas. The model owns
 * no platform, rendering, audio, or persistence objects. It edits the injected
 * SettingsSession and publishes side effects through take_effects().
 */
class SettingsMenuModel final {
public:
    explicit SettingsMenuModel(settings::SettingsSession& session,
                               SettingsMenuEnvironment environment = {});

    [[nodiscard]] settings::SettingsTab active_tab() const noexcept;
    void set_active_tab(settings::SettingsTab tab);
    /** Refreshes externally discovered language packs without discarding the draft. */
    void set_languages(std::vector<SettingsLanguageOption> languages);
    /**
     * Catalogue lookup for KeyControl text (translate_key). With it the
     * Controls values are final text prefixed by `literal_text_prefix`, so ids
     * missing from the pack (ESCAPE, RALT) are never humanised; without it
     * the value is the bare string id.
     */
    void set_key_name_lookup(settings::RetailStringLookup lookup) {
        key_name_lookup_ = std::move(lookup);
    }

    [[nodiscard]] SettingsMenuPresentation presentation() const;
    [[nodiscard]] std::optional<SettingsMenuTarget> focused() const noexcept;
    [[nodiscard]] bool set_focus(SettingsMenuTarget target);

    void pointer_move(std::optional<ui::Point> point);
    void pointer_press(ui::Point point);
    void pointer_drag(ui::Point point);
    void pointer_release(ui::Point point);
    void cancel_pointer_capture() noexcept;
    [[nodiscard]] bool mouse_wheel(ui::Point point, std::int32_t vertical_steps);
    [[nodiscard]] bool handle(ui::InputEvent event);

    /** Captures an SDL/USB keyboard scancode without depending on SDL headers. */
    [[nodiscard]] settings::BindingAssignmentResult capture_scancode(std::uint32_t scancode);
    [[nodiscard]] settings::BindingAssignmentResult capture_mouse_button(std::uint32_t button);
    void cancel_binding_capture() noexcept;

    [[nodiscard]] bool set_category_expanded(SettingsRowId category, bool expanded);
    [[nodiscard]] bool category_expanded(SettingsRowId category) const noexcept;

    void activate_defaults();
    void activate_done();
    void activate_cancel();
    /**
     * The bound Menu key (settingsMenu.on_key_press). In game it restores the
     * draft, plays the back cue and returns straight to the world; in the
     * frontend it is Cancel.
     */
    void activate_menu_key();

    /**
     * EditBoxFloatControl on the sensitivity row. While it has focus, typed
     * text lands in the box and Enter or a click elsewhere commits the value
     * clamped to 0..1 at two decimals.
     */
    [[nodiscard]] bool text_editing() const noexcept;
    [[nodiscard]] bool text_input(std::string_view utf8);
    /** Backspace (`forward == false`) or Delete at the end of the box. */
    [[nodiscard]] bool text_erase(bool forward);
    void commit_text_edit();

    /** Moves all queued commands/effects out in their original event order. */
    [[nodiscard]] std::vector<SettingsMenuEffect> take_effects() noexcept;

private:
    enum class ScrollbarCapture : std::uint8_t {
        none,
        up_arrow,
        down_arrow,
        thumb,
        track,
    };

    [[nodiscard]] std::vector<SettingsRowId> inventory(settings::SettingsTab tab) const;
    [[nodiscard]] std::vector<SettingsRowId> expanded_rows(settings::SettingsTab tab) const;
    [[nodiscard]] std::optional<SettingsMenuTarget> hit_test(ui::Point point) const;
    [[nodiscard]] std::optional<ui::Rect> visible_row_bounds(SettingsRowId row) const;
    [[nodiscard]] std::size_t maximum_scroll_index(settings::SettingsTab tab) const;
    [[nodiscard]] bool target_enabled(SettingsMenuTarget target) const;
    [[nodiscard]] bool activate(SettingsMenuTarget target);
    [[nodiscard]] bool adjust_row(SettingsRowId row, std::int32_t direction);
    [[nodiscard]] bool set_slider_from_pointer(SettingsRowId row, ui::Point point, bool play_sound);
    [[nodiscard]] bool step_volume(SettingsRowId row, std::int32_t direction);
    [[nodiscard]] std::optional<std::size_t> dropdown_option_at(ui::Point point) const;
    [[nodiscard]] ui::Rect dropdown_panel_bounds() const;
    void open_resolution_dropdown();
    void close_resolution_dropdown() noexcept;
    [[nodiscard]] std::size_t visible_row_count(settings::SettingsTab tab) const;
    [[nodiscard]] ui::Rect scrollbar_thumb_bounds() const;
    [[nodiscard]] ScrollbarCapture scrollbar_hit_test(ui::Point point) const;
    [[nodiscard]] bool set_scroll_index(std::size_t value, bool play_sound);
    void scroll_from_track_pointer(ui::Point point, bool play_sound);
    void begin_binding_capture(SettingsRowId row);
    void emit_preview(SettingsRowId source);
    void clamp_scroll(settings::SettingsTab tab);
    void reveal_focused_row();
    void repair_focus();

    settings::SettingsSession* session_{};
    SettingsMenuEnvironment environment_{};
    settings::SettingsTab active_tab_{settings::SettingsTab::main};
    std::array<std::size_t, 3U> scroll_indices_{};
    /** Expansion of each category header, indexed by category_slot(). */
    std::array<bool, 6U> categories_expanded_{true, true, true, true, true, true};
    bool favorite_server_{false};
    bool initial_favorite_server_{false};
    /** Tier restored when the retail Compatibility Shader toggle goes off. */
    settings::ShaderQuality restore_shader_quality_{settings::ShaderQuality::high};
    std::optional<SettingsMenuTarget> focused_{};
    std::optional<SettingsMenuTarget> hovered_{};
    std::optional<SettingsMenuTarget> pressed_{};
    std::optional<ui::Point> pointer_{};
    std::optional<SettingsRowId> dragged_slider_{};
    /** RangeBarControl arrow held down: -1 left, +1 right; fires on release. */
    std::int32_t pressed_range_arrow_{};
    std::optional<std::string> sensitivity_edit_{};
    bool resolution_dropdown_open_{};
    std::size_t resolution_dropdown_first_index_{};
    std::optional<std::size_t> pressed_dropdown_option_{};
    ScrollbarCapture scrollbar_capture_{ScrollbarCapture::none};
    std::optional<settings::ControlAction> binding_capture_{};
    settings::RetailStringLookup key_name_lookup_{};
    std::optional<settings::BindingAssignmentResult> binding_rejection_{};
    std::optional<settings::InputBinding> binding_rejected_input_{};
    std::vector<SettingsMenuEffect> effects_{};
};

/** Text-command prefix the frontend renders verbatim (no catalogue lookup). */
inline constexpr std::string_view literal_text_prefix{"LITERAL|"};

/**
 * KeyControl value text: translate_key() for keys, LMB/RMB for mouse
 * buttons, strings.NONE when unbound (KeyControl.draw's None/'' branch).
 */
[[nodiscard]] std::string settings_binding_text(settings::InputBinding binding,
                                                const settings::RetailStringLookup& lookup);

[[nodiscard]] std::string_view settings_row_name(SettingsRowId row) noexcept;
[[nodiscard]] std::optional<settings::ControlAction>
settings_row_control_action(SettingsRowId row) noexcept;
/** The Controls row label id (e.g. CHANGE_CLASS) bound to one action. */
[[nodiscard]] std::string_view settings_control_action_label(settings::ControlAction action) noexcept;

} // namespace battlespades::frontend
