#pragma once

#include <cstdint>

namespace battlespades::render {

/**
 * Global bgfx view-ID contract.
 *
 * bgfx draws views in ascending ID order, so the 3D world pass must sit
 * between the UI backdrop clear and the UI sprite layers. BgfxUiRenderer
 * owns the clear and both UI layers; WorldRenderer owns the world view and
 * its depth clear. Keep every setView* call behind these constants.
 */
inline constexpr std::uint16_t backdrop_clear_view_id{0U};

/**
 * Reserved for shadow cascades. Depth-only, each with its own framebuffer,
 * and they must precede the world so their maps are resolved before sampling.
 */
inline constexpr std::uint16_t shadow_view_id_base{1U};
inline constexpr std::uint16_t shadow_view_count{4U};

inline constexpr std::uint16_t world_view_id{5U};
/** First-person tool models: depth-cleared over the world, under the UI. */
inline constexpr std::uint16_t view_model_view_id{6U};

/** Reserved for screen-space passes: ambient occlusion and the bloom pyramid. */
inline constexpr std::uint16_t post_view_id_base{7U};
inline constexpr std::uint16_t post_view_count{16U};

/**
 * Resolves the offscreen HDR scene to the backbuffer.
 *
 * Everything the world draws reaches the screen through this view, and every
 * UI view sorts after it, which is what keeps the HUD out of tonemapping and
 * bloom without any UI shader changes.
 */
inline constexpr std::uint16_t composite_view_id{23U};

inline constexpr std::uint16_t ui_window_view_id{24U};
inline constexpr std::uint16_t ui_canvas_view_id{25U};

static_assert(shadow_view_id_base + shadow_view_count <= world_view_id);
static_assert(post_view_id_base + post_view_count <= composite_view_id);
static_assert(composite_view_id < ui_window_view_id);

} // namespace battlespades::render
