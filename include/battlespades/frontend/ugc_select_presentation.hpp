#pragma once

#include "battlespades/frontend/ugc_select_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstddef>
#include <cstdint>

namespace battlespades::frontend {

struct UgcSelectPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Exact DrawList reconstruction of the retail UGC Select scene. */
class UgcSelectPresentation final {
public:
    static constexpr std::size_t layer_command_count{17U};
    static constexpr std::size_t complete_command_count{18U};

    /** Complete standalone frame, including the shared covered background. */
    [[nodiscard]] ui::DrawList build(const UgcSelectMenuModel& menu,
                                     const UgcSelectPresentationContext& context) const;

    /**
     * Design-space foreground consumed by the shared horizontal slide
     * compositor. The compositor owns one stationary background and translates
     * this layer together with the retained previous screen.
     */
    [[nodiscard]] ui::DrawList build_layer(const UgcSelectMenuModel& menu) const;
};

} // namespace battlespades::frontend
