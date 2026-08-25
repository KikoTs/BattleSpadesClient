#pragma once

#include "battlespades/frontend/join_match_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>

namespace battlespades::frontend {

struct JoinMatchPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Draw-list reconstruction of the three-button Join Match scene. */
class JoinMatchPresentation final {
public:
    static constexpr std::size_t layer_command_count{17U};
    static constexpr std::size_t complete_command_count{18U};

    /** Complete standalone frame, including the shared frontend background. */
    [[nodiscard]] ui::DrawList build(const JoinMatchMenuModel& menu,
                                     const JoinMatchPresentationContext& context) const;

    /**
     * Design-space menu layer for the shared horizontal transition compositor.
     *
     * The caller draws one stationary background, translates this layer by
     * `FrontendShellModel::active_offset() * window.width`, then draws the
     * retained previous layer at the shell's absolute
     * `previous_offset() * window.width`.
     */
    [[nodiscard]] ui::DrawList build_layer(const JoinMatchMenuModel& menu) const;
};

/** Draw-list reconstruction of retail `InputServer`. */
class DirectConnectPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const DirectConnectMenuModel& menu,
                                     const JoinMatchPresentationContext& context) const;
    [[nodiscard]] ui::DrawList build_layer(const DirectConnectMenuModel& menu) const;
};

struct ServerBrowserPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
    std::string status_text;
    std::string selected_map_preview_asset;
    std::size_t first_visible_row{};
    std::size_t maximum_visible_rows{15U};
    std::optional<ui::Point> pointer;
    bool pointer_down{};
};

/** Renderer-neutral retail server-list composition. */
class ServerBrowserPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const ServerBrowserModel& browser,
                                     const ServerBrowserPresentationContext& context) const;
    [[nodiscard]] ui::DrawList build_layer(const ServerBrowserModel& browser,
                                           const ServerBrowserPresentationContext& context) const;
};

} // namespace battlespades::frontend
