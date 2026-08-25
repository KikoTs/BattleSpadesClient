#include "battlespades/ui/draw_list.hpp"

#include <utility>

namespace battlespades::ui {

void DrawList::reserve(std::size_t command_count) {
    commands_.reserve(command_count);
}

void DrawList::clear() noexcept {
    commands_.clear();
}

void DrawList::push(SpriteDrawCommand command) {
    commands_.emplace_back(std::move(command));
}

void DrawList::push(TextDrawCommand command) {
    commands_.emplace_back(std::move(command));
}

void DrawList::push(PlayerNamePlateDrawRequest command) {
    commands_.emplace_back(std::move(command));
}

void DrawList::push(DrawCommand command) {
    commands_.push_back(std::move(command));
}

std::span<const DrawCommand> DrawList::commands() const noexcept {
    return commands_;
}

std::span<DrawCommand> DrawList::mutable_commands() noexcept {
    return commands_;
}

std::size_t DrawList::size() const noexcept {
    return commands_.size();
}

bool DrawList::empty() const noexcept {
    return commands_.empty();
}

} // namespace battlespades::ui
