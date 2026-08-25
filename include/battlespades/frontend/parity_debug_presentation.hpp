#pragma once

#include "battlespades/frontend/parity_debug_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>

namespace battlespades::frontend {

struct ParityDebugPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Large-frame inspector for every recovered retail screen and widget class. */
class ParityDebugPresentation final {
public:
    [[nodiscard]] ui::DrawList build(
        const ParityDebugMenuModel& model,
        const ParityDebugPresentationContext& context = {}) const;
};

} // namespace battlespades::frontend
