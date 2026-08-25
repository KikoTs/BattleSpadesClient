#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace battlespades::frontend {

/** Runtime values which do not belong to the persistent Select Menu model. */
struct MainMenuPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::string player_name;
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Fully resolved window-pixel geometry for the deferred welcome/name plate. */
struct PlayerNamePlateGeometry final {
    ui::DrawRect frame{};
    double text_left{};
    double baseline_y{};
    double scale{};

    [[nodiscard]] friend constexpr bool operator==(const PlayerNamePlateGeometry&,
                                                   const PlayerNamePlateGeometry&) = default;
};

/**
 * Resolves the binary-confirmed name-plate formula after text measurement.
 *
 * `content_width_pixels` is measured at the fixed 16-pixel design font size.
 * The returned frame, text origin and baseline are in physical window pixels.
 * Invalid extents, metrics or request constants fail closed.
 */
[[nodiscard]] std::optional<PlayerNamePlateGeometry>
resolve_player_name_plate_geometry(const ui::PlayerNamePlateDrawRequest& request,
                                   double content_width_pixels) noexcept;

/**
 * Converts Select Menu state into an exact ordered Classic-profile draw list.
 *
 * No texture is opened and no glyph is shaped here. Invalid window extents or
 * opacity values throw `std::invalid_argument`; callers should retain their
 * previous frame rather than submitting a partial menu.
 */
class MainMenuPresentation final {
public:
    static constexpr std::size_t command_count{32U};

    [[nodiscard]] ui::DrawList build(const MainMenuModel& menu,
                                     const MainMenuPresentationContext& context) const;
};

} // namespace battlespades::frontend
