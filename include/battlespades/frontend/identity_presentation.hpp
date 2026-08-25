#pragma once

#include "battlespades/frontend/identity_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

namespace battlespades::frontend {

struct IdentityPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t opacity_per_mille{1'000U};
};

/** Draws the account gate with the original Select Menu art vocabulary. */
class IdentityPresentation final {
public:
    [[nodiscard]] ui::DrawList build(
        const IdentityMenuModel& model,
        const IdentityPresentationContext& context) const;
};

} // namespace battlespades::frontend
