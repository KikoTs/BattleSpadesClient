#include "battlespades/ui/draw_list_transform.hpp"

#include <type_traits>

namespace battlespades::ui {

void append_translated(DrawList& destination,
                       const DrawList& source,
                       double design_offset_x,
                       double window_offset_x) {
    destination.reserve(destination.size() + source.size());
    for (const auto& command : source.commands()) {
        std::visit(
            [&](const auto& payload) {
                using Payload = std::decay_t<decltype(payload)>;
                auto translated = payload;
                if constexpr (std::is_same_v<Payload, PlayerNamePlateDrawRequest>) {
                    translated.window_offset_x += window_offset_x;
                } else {
                    const auto offset =
                        translated.space == DrawSpace::design_pixels ? design_offset_x
                                                                    : window_offset_x;
                    translated.destination.x += offset;
                    if constexpr (std::is_same_v<Payload, SpriteDrawCommand>) {
                        if (translated.clip_pixels.has_value()) {
                            translated.clip_pixels->x += offset;
                        }
                    }
                }
                destination.push(std::move(translated));
            },
            command);
    }
}

} // namespace battlespades::ui
