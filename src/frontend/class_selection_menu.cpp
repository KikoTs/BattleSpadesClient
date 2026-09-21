#include "battlespades/frontend/class_selection_menu.hpp"

#include "battlespades/world/class_catalog.hpp"

#include <algorithm>
#include <ranges>

namespace battlespades::frontend {
namespace {

[[nodiscard]] bool inside(ui::Point point, int x, int y, int width, int height) {
    return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
}

} // namespace

void ClassSelectionMenuModel::configure(std::span<const std::uint8_t> available_classes,
                                        std::uint8_t team,
                                        std::uint8_t current_class) {
    classes_.clear();
    for (const auto value : available_classes) {
        if (world::find_class_definition(value) != nullptr &&
            std::ranges::find(classes_, value) == classes_.end()) {
            classes_.push_back(value);
        }
    }
    if (classes_.empty())
        classes_.push_back(current_class);
    if (world::find_class_definition(classes_.front()) == nullptr) {
        classes_.front() = 0U;
    }
    team_ = team;
    const auto found = std::ranges::find(classes_, current_class);
    class_index_ =
        found == classes_.end() ? 0U : static_cast<std::size_t>(found - classes_.begin());
    visible_class_offset_ = 0U;
    reveal_selected_class();
    option_indices_.fill(0U);
    reset_prefabs();
    class_loadouts_.clear();
    hovered_.reset();
    pending_audio_cue_.reset();
}

void ClassSelectionMenuModel::restore_loadout(
    std::span<const std::uint8_t> loadout, std::span<const std::string> prefabs) {
    const auto* definition = world::find_class_definition(selected_class());
    if (definition == nullptr) return;
    for (std::size_t group{}; group < option_indices_.size(); ++group) {
        const auto options = definition->item_groups[group];
        const auto found = std::ranges::find_if(options, [loadout](std::uint16_t item) {
            return std::ranges::find(loadout, item) != loadout.end();
        });
        if (found != options.end()) {
            option_indices_[group] = static_cast<std::size_t>(found - options.begin());
        }
    }
    auto restored = world::make_class_selection(selected_class(), option_indices_, prefabs);
    if (!restored.prefabs.empty()) prefabs_ = std::move(restored.prefabs);
}

std::span<const std::uint8_t> ClassSelectionMenuModel::classes() const noexcept {
    return classes_;
}

std::uint8_t ClassSelectionMenuModel::team() const noexcept {
    return team_;
}

std::uint8_t ClassSelectionMenuModel::selected_class() const noexcept {
    return classes_.empty() ? 0U : classes_[class_index_];
}

std::size_t ClassSelectionMenuModel::selected_class_index() const noexcept {
    return class_index_;
}

std::size_t ClassSelectionMenuModel::classes_per_page() const noexcept {
    return classes_.size() > 4U ? 5U : 4U;
}

ClassSelectionCardLayout ClassSelectionMenuModel::card_layout() const noexcept {
    // selectClass.py and HorizontalListSelection.item_info: <=4 classes use
    // large cards; the five-card strip is only for longer server rosters.
    return classes_per_page() == 4U
               ? ClassSelectionCardLayout{81, 132, 136, 167, 108, 110, 1.0, 1.1}
               : ClassSelectionCardLayout{85, 131, 107, 134, 104, 88, 0.8, 0.88};
}

ui::Rect ClassSelectionMenuModel::class_card_bounds(std::size_t visible_index) const noexcept {
    const auto layout = card_layout();
    return {layout.x + static_cast<int>(visible_index) * layout.interval,
            layout.y, layout.size, layout.size};
}

std::size_t ClassSelectionMenuModel::visible_class_offset() const noexcept {
    return visible_class_offset_;
}

std::array<std::size_t, 4U> ClassSelectionMenuModel::option_indices() const noexcept {
    return option_indices_;
}

std::span<const std::string> ClassSelectionMenuModel::selected_prefabs() const noexcept {
    return prefabs_;
}

world::ClassSelection ClassSelectionMenuModel::selection() const {
    return world::make_class_selection(selected_class(), option_indices_, prefabs_);
}

std::optional<ui::Point> ClassSelectionMenuModel::hovered() const noexcept {
    return hovered_;
}

std::optional<ClassSelectionAudioCue> ClassSelectionMenuModel::take_audio_cue() noexcept {
    auto result = pending_audio_cue_;
    pending_audio_cue_.reset();
    return result;
}

void ClassSelectionMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point;
}

std::optional<ClassSelectionAction> ClassSelectionMenuModel::click(ui::Point point) {
    if (inside(point, 54, 541, 695, 35)) {
        pending_audio_cue_ = ClassSelectionAudioCue::back;
        return ClassSelectionAction::back;
    }
    if (inside(point, 599, 461, 124, 40)) {
        pending_audio_cue_ = ClassSelectionAudioCue::confirm;
        return ClassSelectionAction::submit;
    }

    const auto offset = visible_class_offset();
    const auto visible = std::min(classes_per_page(), classes_.size() - offset);
    for (std::size_t index{}; index < visible; ++index) {
        if (class_card_bounds(index).contains(point)) {
            select_class(offset + index);
            pending_audio_cue_ = ClassSelectionAudioCue::scroll;
            return std::nullopt;
        }
    }
    if (classes_.size() > classes_per_page()) {
        if (inside(point, 70, 250, 22, 22)) {
            scroll_classes(-1);
            pending_audio_cue_ = ClassSelectionAudioCue::scroll;
            return std::nullopt;
        }
        if (inside(point, 708, 250, 22, 22)) {
            scroll_classes(1);
            pending_audio_cue_ = ClassSelectionAudioCue::scroll;
            return std::nullopt;
        }
    }
    const auto* definition = world::find_class_definition(selected_class());
    if (definition == nullptr)
        return std::nullopt;
    for (std::size_t group{}; group < option_indices_.size(); ++group) {
        const auto options = definition->item_groups[group];
        for (std::size_t option{}; option < std::min<std::size_t>(options.size(), 6U); ++option) {
            if (inside(point,
                       171 + static_cast<int>(option) * 45,
                       294 + static_cast<int>(group) * 53,
                       42,
                       42)) {
                option_indices_[group] = option;
                pending_audio_cue_ = ClassSelectionAudioCue::scroll;
                return std::nullopt;
            }
        }
    }
    const auto options = world::class_prefab_options(selected_class());
    for (std::size_t index{}; index < std::min<std::size_t>(options.size(), 12U); ++index) {
        const auto column = static_cast<int>(index % 3U);
        const auto row = static_cast<int>(index / 3U);
        if (!inside(point, 469 + column * 41, 313 + row * 43, 38, 38)) {
            continue;
        }
        const std::string name{options[index]};
        const auto found = std::ranges::find(prefabs_, name);
        if (found != prefabs_.end()) {
            // Retail TableSelection allows one to three constructs.
            if (prefabs_.size() > 1U) prefabs_.erase(found);
        } else {
            // At capacity, replace the oldest choice exactly as retail's
            // HorizontalListSelection.on_item_selected does.
            if (prefabs_.size() >= 3U) prefabs_.erase(prefabs_.begin());
            prefabs_.push_back(name);
        }
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
        return std::nullopt;
    }
    return std::nullopt;
}

void ClassSelectionMenuModel::cycle_class(int direction) noexcept {
    if (classes_.empty() || direction == 0)
        return;
    const auto size = static_cast<int>(classes_.size());
    select_class(static_cast<std::size_t>(
        (static_cast<int>(class_index_) + (direction > 0 ? 1 : -1) + size) % size));
    pending_audio_cue_ = ClassSelectionAudioCue::scroll;
}

void ClassSelectionMenuModel::scroll_classes(int direction) noexcept {
    const auto per_page = classes_per_page();
    if (direction == 0 || classes_.size() <= per_page)
        return;
    const auto maximum = static_cast<int>(classes_.size() - per_page);
    const auto previous = visible_class_offset_;
    visible_class_offset_ = static_cast<std::size_t>(
        std::clamp(static_cast<int>(visible_class_offset_) + (direction > 0 ? 1 : -1), 0, maximum));
    if (visible_class_offset_ != previous) {
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
    }
}

void ClassSelectionMenuModel::cycle_group(std::size_t group, int direction) noexcept {
    const auto* definition = world::find_class_definition(selected_class());
    if (definition == nullptr || group >= option_indices_.size() || direction == 0)
        return;
    const auto options = definition->item_groups[group];
    if (options.empty())
        return;
    const auto size = static_cast<int>(options.size());
    option_indices_[group] = static_cast<std::size_t>(
        (static_cast<int>(option_indices_[group]) + (direction > 0 ? 1 : -1) + size) % size);
    pending_audio_cue_ = ClassSelectionAudioCue::scroll;
}

void ClassSelectionMenuModel::select_visible_class(std::size_t visible_index) noexcept {
    const auto index = visible_class_offset() + visible_index;
    if (visible_index < classes_per_page() && index < classes_.size()) {
        select_class(index);
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
    }
}

void ClassSelectionMenuModel::select_class(std::size_t index) noexcept {
    if (index >= classes_.size())
        return;
    if (index == class_index_) {
        reveal_selected_class();
        return;
    }
    class_loadouts_[selected_class()] = {option_indices_, prefabs_};
    class_index_ = index;
    reveal_selected_class();
    const auto saved = class_loadouts_.find(selected_class());
    if (saved != class_loadouts_.end()) {
        option_indices_ = saved->second.options;
        prefabs_ = saved->second.prefabs;
    } else {
        option_indices_.fill(0U);
        reset_prefabs();
    }
}

void ClassSelectionMenuModel::reveal_selected_class() noexcept {
    const auto per_page = classes_per_page();
    if (classes_.size() <= per_page) {
        visible_class_offset_ = 0U;
        return;
    }
    if (class_index_ < visible_class_offset_) {
        visible_class_offset_ = class_index_;
    } else if (class_index_ >= visible_class_offset_ + per_page) {
        // Retail HorizontalListSelection.on_itemindex_selected scrolls only
        // far enough to expose the requested card; it never jumps a page.
        visible_class_offset_ = class_index_ - per_page + 1U;
    }
    visible_class_offset_ = std::min(visible_class_offset_, classes_.size() - per_page);
}

void ClassSelectionMenuModel::reset_prefabs() {
    prefabs_.clear();
    for (const auto name : world::class_prefab_options(selected_class())) {
        if (prefabs_.size() == 3U)
            break;
        prefabs_.emplace_back(name);
    }
}

} // namespace battlespades::frontend
