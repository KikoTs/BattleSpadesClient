#pragma once

#include "battlespades/ui/draw_list.hpp"
#include "battlespades/world/gameplay_debug_lab.hpp"

namespace battlespades::frontend {

/** Transparent in-world HUD for the F10 class/weapon developer laboratory. */
class GameplayDebugPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const world::GameplayDebugLab& lab,
                                     ui::PixelExtent window) const;
};

} // namespace battlespades::frontend
