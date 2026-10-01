#pragma once

#include "battlespades/settings/client_settings.hpp"

namespace battlespades::settings {

/**
 * What a committed graphics change costs the running client.
 *
 * Every field names a mechanism, not a menu row, so the frontend can apply a
 * change in a match without guessing: `display` is an SDL mode change followed
 * by a bgfx resize, `vertical_sync` and a live `multisample` change are a
 * bgfx::reset, `terrain_remesh` goes through the bounded live re-mesh lane and
 * `per_frame` values are read by the renderer every frame. Nothing here tears
 * down the renderer or touches the network session.
 */
struct GraphicsApplyPlan final {
    /** Resolution, fullscreen or fullscreen kind: SDL mode change + bgfx resize. */
    bool display{};
    /** bgfx::reset with the new VSync flag; the swap chain is resized, not replaced. */
    bool vertical_sync{};
    /** The antialiasing choice changed at all. */
    bool multisample{};
    /**
     * The new sample count cannot reach the running swap chain and waits for
     * the next launch. On Direct3D 11/12 bgfx destroys and recreates the swap
     * chain to change it, which fails fatally ("Failed to create swap chain")
     * while the Steam overlay still holds the old one.
     */
    bool multisample_deferred{};
    /** Shader tier or effect quality: one renderer profile resolve per frame. */
    bool shader_profile{};
    /**
     * The tier crossed the Legacy/enhanced boundary, which changes what the
     * chunk mesher bakes; the resident terrain is re-meshed over several frames.
     */
    bool terrain_remesh{};
    /** Draw distance, render interpolation or HUD scale: read every frame. */
    bool per_frame{};
    /** Graphics API, texture or model quality: startup-only resources. */
    bool restart_only{};

    /** True when something the player changed will only show after a restart. */
    [[nodiscard]] constexpr bool restart_required() const noexcept {
        return restart_only || multisample_deferred;
    }

    /** True when the running client has work to do now. */
    [[nodiscard]] constexpr bool live_work() const noexcept {
        return display || vertical_sync || (multisample && !multisample_deferred) ||
               shader_profile || terrain_remesh || per_frame;
    }

    [[nodiscard]] friend constexpr bool operator==(const GraphicsApplyPlan&,
                                                   const GraphicsApplyPlan&) = default;
};

/**
 * Classifies the difference between the settings the client is running with
 * and the ones just committed.
 *
 * `multisample_live` is a property of the active renderer backend (see
 * render::multisample_change_is_live); this layer stays GPU-free.
 */
[[nodiscard]] GraphicsApplyPlan plan_graphics_apply(const ClientSettings& active,
                                                    const ClientSettings& requested,
                                                    bool multisample_live) noexcept;

} // namespace battlespades::settings
