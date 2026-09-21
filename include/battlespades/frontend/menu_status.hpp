#pragma once

#include "battlespades/core/utf8.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::frontend {

/** A bounded notice in the 800x600 menu canvas. An empty title makes a strip. */
inline void append_menu_status(ui::DrawList& list, ui::DrawRect bounds,
                               std::string_view title, std::string_view message,
                               ui::ColorRgba8 accent = {215U, 189U, 83U, 255U}) {
    if (message.empty() || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
        bounds.width <= 0.0 || bounds.height <= 0.0) return;
    // Keep the entire surface inside the design canvas even when a caller
    // supplies a notice wider than its panel or anchors it near an edge.
    bounds.width = std::min(bounds.width, 784.0);
    bounds.height = std::min(bounds.height, 584.0);
    bounds.x = std::clamp(bounds.x, 8.0, 792.0 - bounds.width);
    bounds.y = std::clamp(bounds.y, 8.0, 592.0 - bounds.height);
    if (bounds.width < 32.0 || bounds.height < 24.0) return;

    const bool show_title = !title.empty() && bounds.height >= 54.0;
    ui::SpriteDrawCommand panel;
    panel.asset_id = "png/high/white.png";
    panel.destination = bounds;
    panel.modulation.color = {27U, 30U, 1U, 255U};
    list.push(panel);
    panel.destination = {bounds.x, bounds.y, bounds.width, 1.0};
    panel.modulation.color = {76U, 79U, 29U, 255U};
    list.push(panel);
    panel.destination.y = bounds.y + bounds.height - 1.0;
    list.push(panel);
    panel.destination = {bounds.x, bounds.y, 3.0, bounds.height};
    panel.modulation.color = accent;
    list.push(panel);
    ui::TextDrawCommand heading;
    heading.preferred_font_asset = "fonts/A750-Sans-Medium.ttf";
    heading.layout = ui::TextLayout::bounded_wrapped_lines;
    if (show_title) {
        heading.localization_key = core::utf8_code_point_prefix(title, 80U);
        heading.destination = {bounds.x + 12.0, bounds.y + 5.0, bounds.width - 24.0, 14.0};
        heading.requested_font_size_pixels = 11.0;
        heading.modulation.color = accent;
        list.push(heading);
    }
    // Diagnostic log tails stay in the log. They must not shrink a user-facing
    // sentence to a few pixels or push text over Leave/Start controls.
    const auto first_line = message.substr(0U, message.find_first_of("\r\n"));
    heading.localization_key = core::utf8_code_point_prefix(first_line, 240U);
    const auto body_top = show_title ? 21.0 : 5.0;
    heading.destination = {bounds.x + 12.0, bounds.y + body_top,
                           bounds.width - 24.0, bounds.height - body_top - 5.0};
    heading.requested_font_size_pixels = 13.0;
    heading.maximum_lines = show_title ? 2U : 1U;
    heading.modulation.color = {244U, 236U, 187U, 255U};
    list.push(heading);
}

} // namespace battlespades::frontend
