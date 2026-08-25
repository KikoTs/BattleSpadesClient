#pragma once

#include "battlespades/core/application.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace battlespades::core {

enum class LaunchAction {
    run,
    show_help,
    show_version,
};

struct LaunchOptions final {
    LaunchAction action{LaunchAction::run};
    RuntimeConfig runtime{};
    bool headless{false};
    /** True when --ticks or --run-forever explicitly selected a lifetime. */
    bool runtime_lifetime_explicit{false};
    /** Developer automation: grant the Tutorial catalogue and equip this tool. */
    std::optional<std::uint8_t> tutorial_debug_tool;
    /** Developer automation: enter the selected tool's recovered aim state. */
    bool tutorial_debug_aim{false};
    /** Developer/live-smoke shortcut through the normal Protocol 168 loader. */
    std::optional<std::string> startup_endpoint;
    /**
     * Offline visual oracle: open the production particle renderer directly
     * and replay one named effect. This deliberately bypasses identity/menu
     * automation so a parity capture cannot accidentally photograph a menu.
     */
    std::optional<std::string> debug_vfx;
    /** Exact simulated age at which the offline VFX oracle freezes. */
    std::optional<double> debug_vfx_age;
    /** Offline production-UI oracle: leaderboard, scoreboard, endgame, chat, or vote. */
    std::optional<std::string> debug_ui;
    /** Offline drag/resize editor for the currently rendered UI screen. */
    bool ui_layout_editor{};
    /**
     * Developer visual verification: load a different map into the Tutorial.
     *
     * A validated basename from assets/original/maps, without the extension.
     * Lets a lighting change be judged against a night city or a desert rather
     * than only against Training's overcast sky.
     */
    std::optional<std::string> tutorial_map;
    /** Developer visual verification: override the skydome, e.g. `Tokyo.txt`. */
    std::optional<std::string> tutorial_skydome;
    /**
     * Developer visual verification: spawn at this canonical voxel position.
     *
     * Training spawns inside an enclosed corridor, so an outdoor shot otherwise
     * depends on synthetic input actually reaching the window.
     */
    std::optional<std::array<double, 3U>> tutorial_spawn;
    /**
     * Developer visual verification: stand on the ground nearest this column.
     *
     * The sibling of `tutorial_spawn` that does not require knowing the height.
     * A map's canonical z depends on transforms the loader applies -- centring
     * and a shift that drops the floor onto the bed -- so computing a standable
     * z outside the engine is error-prone, and getting it wrong fails silently
     * by falling. This defers to `resolve_map_spawn`, which reuses the real
     * ground predicate.
     */
    std::optional<std::array<double, 2U>> tutorial_stand;
    /**
     * Developer visual verification: aim the camera at this canonical position.
     *
     * A point to look AT rather than a yaw/pitch pair, because a target needs no
     * agreement about which way yaw 0 faces or which sign pitch takes -- the
     * orientation is just the normalised difference from the spawn. That makes a
     * capture script's intent ("frame that tower") readable and stable.
     */
    std::optional<std::array<double, 3U>> tutorial_look;
    /**
     * Developer visual verification: force a shader tier for this run only.
     *
     * One of the on-disk tier names. Held as a string rather than a
     * `settings::ShaderQuality` because `aos_core` deliberately does not link
     * `aos_settings`; the frontend resolves it through the one shared parser, so
     * an unknown name is still rejected rather than silently ignored.
     *
     * Exists so a lighting or shadow change can be captured at every tier from a
     * single build, without editing the player's settings file.
     */
    std::optional<std::string> shader_quality;
};

struct ParseResult final {
    std::optional<LaunchOptions> options;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return options.has_value();
    }
};

/**
 * Parses only bootstrap options. Future frontend settings belong to the
 * configuration layer rather than being coupled to SDL or another backend.
 */
[[nodiscard]] ParseResult parse_command_line(std::span<const std::string_view> arguments);

[[nodiscard]] std::string_view command_line_usage() noexcept;

} // namespace battlespades::core
