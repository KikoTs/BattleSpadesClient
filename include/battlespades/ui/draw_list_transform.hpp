#pragma once

#include "battlespades/ui/draw_list.hpp"

namespace battlespades::ui {

/**
 * Appends a horizontally translated copy of a draw list.
 *
 * Design-space and window-space commands use different widths during a menu
 * slide. The caller therefore supplies both offsets. Player-name plates are
 * window-relative deferred requests; their safe-edge geometry is translated
 * by recording a window offset in a resolved command before submission.
 */
void append_translated(DrawList& destination,
                       const DrawList& source,
                       double design_offset_x,
                       double window_offset_x);

} // namespace battlespades::ui
