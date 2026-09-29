#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::settings {

inline constexpr std::uint32_t current_settings_schema_version{1U};

enum class SettingsTab : std::uint8_t {
    main,
    graphics,
    controls,
};

/** Preferences exposed by the retail Main settings tab. */
struct MainSettings final {
    /** BCP-47-style locale selected from the external localization directory. */
    std::string language{"en"};
    double master_volume{1.0};
    double music_volume{1.0};
    bool fullscreen{true};
    bool invert_mouse{false};
    bool show_skins{true};
    bool show_other_skins{true};
    bool weapon_motion{true};
    /**
     * Native-only jetpack/parachute key hints. Retail shows just the status
     * icons, so this is off by default (retail parity decision D4).
     */
    bool ability_hints{false};
    /** Playback endpoint for the next launch; empty follows the system default. */
    std::string audio_device;

    /** Local presentation only; never changes inventory or advertised equipment. */
    [[nodiscard]] constexpr bool skins_visible(bool local_player) const noexcept {
        return show_skins && (local_player || show_other_skins);
    }

    [[nodiscard]] friend constexpr bool operator==(const MainSettings&,
                                                   const MainSettings&) = default;
};

/** A validated display size. Refresh rate was not exposed by the retail menu. */
struct Resolution final {
    std::uint32_t width{800U};
    std::uint32_t height{600U};

    [[nodiscard]] friend constexpr bool operator==(const Resolution&, const Resolution&) = default;
};

enum class QualityLevel : std::uint8_t {
    low,
    medium,
    high,
};

enum class DrawDistance : std::uint16_t {
    low = 90U,
    medium = 128U,
    high = 192U,
};

enum class Antialiasing : std::uint8_t {
    off = 0U,
    samples_2 = 2U,
    samples_4 = 4U,
};

/**
 * Graphics API requested for the next client launch.
 *
 * `automatic` lets bgfx choose the native platform default. The remaining
 * values are portable preferences: the native frontend filters the Settings
 * row against the backends compiled into the current executable and falls
 * back to `automatic` if a settings file is moved to an incompatible platform.
 */
enum class GraphicsApi : std::uint8_t {
    automatic,
    direct3d11,
    direct3d12,
    vulkan,
    opengl,
    metal,
};

[[nodiscard]] constexpr std::string_view graphics_api_name(GraphicsApi value) noexcept {
    switch (value) {
    case GraphicsApi::automatic:
        return "auto";
    case GraphicsApi::direct3d11:
        return "direct3d11";
    case GraphicsApi::direct3d12:
        return "direct3d12";
    case GraphicsApi::vulkan:
        return "vulkan";
    case GraphicsApi::opengl:
        return "opengl";
    case GraphicsApi::metal:
        return "metal";
    }
    return "auto";
}

[[nodiscard]] constexpr std::optional<GraphicsApi>
parse_graphics_api(std::string_view value) noexcept {
    constexpr std::array<GraphicsApi, 6U> backends{
        GraphicsApi::automatic,
        GraphicsApi::direct3d11,
        GraphicsApi::direct3d12,
        GraphicsApi::vulkan,
        GraphicsApi::opengl,
        GraphicsApi::metal,
    };
    for (const auto backend : backends) {
        if (graphics_api_name(backend) == value) {
            return backend;
        }
    }
    return std::nullopt;
}

/**
 * Renderer tier. Retail represented the compatibility shader as level -1.
 *
 * `compatibility` is the Legacy path: the recovered baked face/occlusion
 * shading drawn straight to the backbuffer, kept reachable so retail
 * screenshot parity stays runnable. `ultra` extends the retail range.
 */
enum class ShaderQuality : std::int8_t {
    compatibility = -1,
    low = 0,
    medium = 1,
    high = 2,
    ultra = 3,
};

/**
 * The on-disk spelling of a tier, and its inverse.
 *
 * Public and paired on purpose. These began as two private tables inside the
 * settings store, and when `ultra` was added to the enum but to neither table
 * the result was silent and destructive: saving rewrote Ultra as "medium", and
 * loading rejected the whole file. One shared definition makes adding a tier
 * without updating the mapping a compile error instead of a data-loss bug, and
 * lets the launcher accept the same vocabulary the file uses.
 */
[[nodiscard]] constexpr std::string_view shader_quality_name(ShaderQuality value) noexcept {
    switch (value) {
    case ShaderQuality::compatibility:
        return "compatibility";
    case ShaderQuality::low:
        return "low";
    case ShaderQuality::medium:
        return "medium";
    case ShaderQuality::high:
        return "high";
    case ShaderQuality::ultra:
        return "ultra";
    }
    return "medium";
}

[[nodiscard]] constexpr std::optional<ShaderQuality>
parse_shader_quality(std::string_view value) noexcept {
    constexpr std::array<ShaderQuality, 5U> tiers{ShaderQuality::compatibility,
                                                  ShaderQuality::low,
                                                  ShaderQuality::medium,
                                                  ShaderQuality::high,
                                                  ShaderQuality::ultra};
    for (const auto tier : tiers) {
        if (shader_quality_name(tier) == value) {
            return tier;
        }
    }
    return std::nullopt;
}

/** Preferences exposed by the retail Graphics settings tab. */
struct GraphicsSettings final {
    Resolution resolution{};
    GraphicsApi graphics_api{GraphicsApi::automatic};
    Antialiasing antialiasing{Antialiasing::off};
    QualityLevel effect_quality{QualityLevel::medium};
    DrawDistance draw_distance{DrawDistance::high};
    ShaderQuality shader_quality{ShaderQuality::medium};
    QualityLevel texture_quality{QualityLevel::medium};
    QualityLevel model_quality{QualityLevel::high};
    bool vsync{false};
    /**
     * Native-only (settings.toml `fullscreen_mode`). Borderless covers the
     * desktop at its current mode so alt-tab is instant and never minimises
     * the game; `exclusive` keeps the retail display-mode switch.
     */
    bool borderless_fullscreen{true};
    /**
     * Native-only (settings.toml `render_interpolation`). Extra render-only
     * frames between the fixed 60 Hz ticks on high-refresh displays, with the
     * camera interpolated between the last two ticks. The simulation, input
     * and ClientData cadence are unchanged; `false` restores the retail
     * one-frame-per-tick presentation.
     */
    bool render_interpolation{true};
    /**
     * Native-only (settings.toml `hud_scale`). Retail draws the in-game HUD
     * in raw window pixels, so it shrinks on 4K/Retina displays. 1.0 keeps
     * that retail behaviour; larger values magnify every HUD widget about
     * the window as if the window were that many times smaller (layout
     * proportions unchanged); 0 picks max(1, floor(height / 1080)).
     */
    double hud_scale{1.0};

    [[nodiscard]] constexpr bool compatibility_shader() const noexcept {
        return shader_quality == ShaderQuality::compatibility;
    }

    [[nodiscard]] friend constexpr bool operator==(const GraphicsSettings&,
                                                   const GraphicsSettings&) = default;
};

enum class BindingKind : std::uint8_t {
    unbound,
    keyboard_scancode,
    mouse_button,
};

/**
 * One physical input binding.
 *
 * Keyboard codes are SDL-compatible USB scancodes and therefore do not change
 * with the active keyboard layout. Mouse button codes use SDL's 1-based
 * button numbering. Modifiers were not part of the recovered retail binding
 * model.
 */
struct InputBinding final {
    BindingKind kind{BindingKind::unbound};
    std::uint32_t code{};

    [[nodiscard]] static constexpr InputBinding unbound() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr InputBinding keyboard(std::uint32_t scancode) noexcept {
        return {BindingKind::keyboard_scancode, scancode};
    }

    [[nodiscard]] static constexpr InputBinding mouse(std::uint32_t button) noexcept {
        return {BindingKind::mouse_button, button};
    }

    [[nodiscard]] constexpr bool is_unbound() const noexcept {
        return kind == BindingKind::unbound;
    }

    [[nodiscard]] friend constexpr bool operator==(const InputBinding&,
                                                   const InputBinding&) = default;
};

/** Configurable rows recovered from the Main Game and UGC Controls categories. */
enum class ControlAction : std::uint8_t {
    forward,
    backward,
    left,
    right,
    sneak,
    crouch,
    sprint,
    jump,
    aim,
    reload,
    team_chat,
    global_chat,
    show_map,
    view_scores,
    change_team,
    change_class,
    menu,
    weapon_custom,
    map_vote_1,
    map_vote_2,
    map_vote_3,
    kick_player,
    toggle_hud,
    ugc_settings,
    tool_help,
    palette_left,
    palette_right,
    palette_up,
    palette_down,
    cancel_prefab_placement,
    carve_prefab,
    hover,
    quick_save,
    count,
};

inline constexpr std::size_t control_action_count = static_cast<std::size_t>(ControlAction::count);

/** Preferences exposed by the retail Controls settings tab. */
struct ControlsSettings final {
    double mouse_sensitivity{0.1};
    std::array<InputBinding, control_action_count> bindings{};

    [[nodiscard]] InputBinding binding(ControlAction action) const noexcept;
    [[nodiscard]] bool set_binding(ControlAction action, InputBinding binding) noexcept;

    [[nodiscard]] friend constexpr bool operator==(const ControlsSettings&,
                                                   const ControlsSettings&) = default;
};

/** Complete versioned client configuration persisted by SettingsStore. */
struct ClientSettings final {
    std::uint32_t schema_version{current_settings_schema_version};
    MainSettings main{};
    GraphicsSettings graphics{};
    ControlsSettings controls{};

    [[nodiscard]] friend constexpr bool operator==(const ClientSettings&,
                                                   const ClientSettings&) = default;
};

enum class BindingAssignmentStatus : std::uint8_t {
    assigned,
    invalid_action,
    invalid_binding,
    reserved_inventory_key,
    conflicts_with_existing_action,
};

struct BindingAssignmentResult final {
    BindingAssignmentStatus status{BindingAssignmentStatus::assigned};
    std::optional<ControlAction> conflicting_action{};

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return status == BindingAssignmentStatus::assigned;
    }
};

struct SettingsValidationResult final {
    bool valid{true};
    std::string error{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid;
    }
};

/** Returns the exact defaults recovered from the retail client configuration. */
[[nodiscard]] ClientSettings retail_default_settings() noexcept;

/**
 * Clamps scalar values and replaces invalid enum/binding values with safe
 * retail defaults. Conflicting bindings are resolved deterministically.
 */
[[nodiscard]] ClientSettings normalize_settings(const ClientSettings& settings) noexcept;

/** Strictly validates a settings snapshot without modifying it. */
[[nodiscard]] SettingsValidationResult validate_settings(const ClientSettings& settings);

[[nodiscard]] bool valid_binding(InputBinding binding) noexcept;
[[nodiscard]] bool is_reserved_inventory_binding(InputBinding binding) noexcept;

/** Stable TOML identifiers used for every configurable Controls row. */
[[nodiscard]] std::string_view control_action_name(ControlAction action) noexcept;
[[nodiscard]] std::optional<ControlAction> control_action_from_name(std::string_view name) noexcept;

/** Human-readable persistence representation, for example `keyboard:w`. */
[[nodiscard]] std::string binding_to_string(InputBinding binding);
[[nodiscard]] std::optional<InputBinding> binding_from_string(std::string_view value) noexcept;

/**
 * Retail gui.translate_key() identifier for a keyboard scancode: the pyglet
 * symbol name with the leading '_' / 'NUM_' removed and KEY_TRANSLATIONS
 * applied (LCTRL -> CTRL, LSHIFT -> SHIFT, PAGEUP -> PAGE_UP, ...). The
 * caller resolves it through strings.get_by_id, which leaves ids missing from
 * the string table (letters, F-keys) unchanged. Empty for unknown codes.
 */
[[nodiscard]] std::string_view retail_key_name_id(std::uint32_t scancode) noexcept;

/**
 * Controls-menu value id for one binding: retail_key_name_id for keys,
 * LMB/MMB/RMB for mouse buttons and NONE ("None") when unbound.
 */
[[nodiscard]] std::string retail_binding_name_id(InputBinding binding);

} // namespace battlespades::settings
