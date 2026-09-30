#include "battlespades/frontend/class_selection_menu.hpp"

#include "battlespades/world/class_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace battlespades::frontend {
namespace {

[[nodiscard]] bool inside(ui::Point point, int x, int y, int width, int height) {
    return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
}

[[nodiscard]] bool inside(ui::Point point, ui::Rect rect) {
    return inside(point, rect.x, rect.y, rect.width, rect.height);
}

// HorizontalScrollBar(SELECT_CLASS_SCROLLBAR_X=70, _Y=340, _WIDTH=660, 20,
// len(classes), 5) with button_size 22 and button_bar_gap 2.
constexpr double scrollbar_origin{70.0};
constexpr double scrollbar_button{22.0};
constexpr double scrollbar_gap{2.0};
constexpr double scrollbar_full_length{660.0 - scrollbar_button * 2.0 - scrollbar_gap * 2.0};
constexpr double scrollbar_coord_at_min{scrollbar_origin + scrollbar_button + scrollbar_gap - 1.0};
constexpr int scrollbar_bar_top{250};
constexpr int scrollbar_bar_thickness{20};
constexpr std::size_t scrollbar_visible{5U};

// LOADOUT_HLIST rows (x=316, y-up 291 - 53g) and the prefab TableSelection.
constexpr int loadout_cell_x{171};
constexpr int loadout_cell_y{294};
constexpr int loadout_cell_step_x{45};
constexpr int loadout_row_step{53};
constexpr int loadout_cell_size{42};
constexpr int construct_cell_x{469};
constexpr int construct_cell_y{313};
constexpr int construct_step_x{41};
constexpr int construct_step_y{43};
constexpr int construct_cell_size{38};
constexpr ui::Rect construct_page_back{469, 484, 14, 14};
constexpr ui::Rect construct_page_next{574, 484, 14, 14};
constexpr ui::Rect frontend_navbar_back{54, 541, 695, 35};

} // namespace

double retail_class_thumb_length(std::size_t classes, std::size_t visible) noexcept {
    double percentage{100.0};
    if (classes != 0U) {
        percentage = 100.0 * static_cast<double>(visible) / static_cast<double>(classes);
    }
    percentage = std::min(percentage, 100.0);
    return std::floor(scrollbar_full_length * percentage / 100.0);
}

std::size_t retail_drag_scroll_index(double value, std::size_t max_scroll) noexcept {
    if (value <= 0.0 || max_scroll == 0U) return 0U;
    const auto maximum = static_cast<double>(max_scroll);
    if (value >= maximum) return max_scroll;
    return static_cast<std::size_t>(1.0 + value * (maximum - 1.0) / maximum);
}

void ClassSelectionMenuModel::configure(std::span<const std::uint8_t> available_classes,
                                        std::uint8_t team,
                                        std::uint8_t current_class,
                                        world::ClassSelectionRules rules,
                                        bool locked_class,
                                        bool in_game) {
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
    rules_ = std::move(rules);
    locked_class_ = locked_class;
    in_game_ = in_game;
    const auto found = std::ranges::find(classes_, current_class);
    class_index_ =
        found == classes_.end() ? 0U : static_cast<std::size_t>(found - classes_.begin());
    visible_class_offset_ = 0U;
    scroll_float_ = 0.0;
    reveal_selected_class();
    class_loadouts_.clear();
    rebuild_options();
    reset_loadout();
    hovered_.reset();
    hovered_item_.reset();
    cancel_pointer();
    held_key_.reset();
    pending_audio_cue_.reset();
}

void ClassSelectionMenuModel::rebuild_options() {
    for (std::size_t group{}; group < row_options_.size(); ++group) {
        row_options_[group] = world::class_row_options(selected_class(), group, rules_);
    }
    construct_options_ = world::class_construct_options(selected_class(), rules_);
    construct_page_ = 0U;
}

void ClassSelectionMenuModel::reset_loadout() {
    // GameClass(config): each row opens on the saved item (config loadout<N>).
    option_indices_ = world::saved_row_indices(selected_class(), rules_);
    prefabs_ = world::default_class_constructs(selected_class(), rules_);
}

std::map<std::uint8_t, world::SavedClassLoadout>
ClassSelectionMenuModel::session_loadouts(bool confirmed) const {
    std::map<std::uint8_t, world::SavedClassLoadout> result;
    const auto record = [this, &result](std::uint8_t class_id,
                                        const std::array<std::size_t, 4U>& options,
                                        const std::vector<std::string>& constructs) {
        std::vector<std::uint16_t> chosen;
        for (std::size_t group{}; group < options.size(); ++group) {
            const auto row = world::class_row_options(class_id, group, rules_);
            if (!row.empty()) chosen.push_back(row[options[group] % row.size()]);
        }
        auto selection = world::make_class_selection(class_id, chosen, constructs, rules_);
        world::SavedClassLoadout saved;
        if (std::ranges::find(selection.loadout, world::flare_block_tool) !=
            selection.loadout.end()) {
            saved.prefabs.emplace_back(world::flare_block_construct);
        }
        for (auto& name : selection.prefabs) saved.prefabs.push_back(std::move(name));
        saved.loadout = std::move(selection.loadout);
        result.insert_or_assign(class_id, std::move(saved));
    };
    for (const auto& [class_id, saved] : class_loadouts_) {
        record(class_id, saved.options, saved.prefabs);
    }
    if (confirmed && !classes_.empty()) record(selected_class(), option_indices_, prefabs_);
    return result;
}

void ClassSelectionMenuModel::restore_playing_loadout(
    std::uint8_t player_team, std::uint8_t player_class,
    std::span<const std::uint8_t> loadout, std::span<const std::string> prefabs) {
    if ((player_team != 2U && player_team != 3U) || player_class != selected_class()) {
        return;
    }
    for (std::size_t group{}; group < option_indices_.size(); ++group) {
        const auto& options = row_options_[group];
        const auto found = std::ranges::find_if(options, [loadout](std::uint16_t item) {
            return std::ranges::find(loadout, item) != loadout.end();
        });
        if (found != options.end()) {
            option_indices_[group] = static_cast<std::size_t>(found - options.begin());
        }
    }
    std::vector<std::string> restored;
    const bool flare_offered =
        std::ranges::find(construct_options_, std::string{world::flare_block_construct}) !=
        construct_options_.end();
    if (flare_offered &&
        std::ranges::find(loadout, world::flare_block_tool) != loadout.end()) {
        restored.emplace_back(world::flare_block_construct);
    }
    for (const auto& name : prefabs) {
        if (restored.size() == 3U) break;
        if (std::ranges::find(construct_options_, name) != construct_options_.end() &&
            std::ranges::find(restored, name) == restored.end()) {
            restored.push_back(name);
        }
    }
    if (!restored.empty()) prefabs_ = std::move(restored);
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

std::span<const std::uint16_t>
ClassSelectionMenuModel::row_options(std::size_t group) const noexcept {
    if (group >= row_options_.size()) return {};
    return row_options_[group];
}

std::span<const std::string> ClassSelectionMenuModel::construct_options() const noexcept {
    return construct_options_;
}

std::size_t ClassSelectionMenuModel::construct_page_count() const noexcept {
    return std::max<std::size_t>(
        1U, (construct_options_.size() + constructs_per_page - 1U) / constructs_per_page);
}

std::span<const std::string> ClassSelectionMenuModel::selected_prefabs() const noexcept {
    return prefabs_;
}

world::ClassSelection ClassSelectionMenuModel::selection() const {
    std::vector<std::uint16_t> chosen;
    for (std::size_t group{}; group < row_options_.size(); ++group) {
        const auto& options = row_options_[group];
        if (!options.empty()) chosen.push_back(options[option_indices_[group] % options.size()]);
    }
    return world::make_class_selection(selected_class(), chosen, prefabs_, rules_);
}

std::optional<ui::Point> ClassSelectionMenuModel::hovered() const noexcept {
    return hovered_;
}

std::optional<ClassSelectionHover> ClassSelectionMenuModel::hovered_item() const noexcept {
    return hovered_item_;
}

bool ClassSelectionMenuModel::popup_visible(Clock::time_point now) const noexcept {
    if (!hovered_item_.has_value() || dragging_) return false;
    return std::chrono::duration<double>(now - hover_started_).count() > popup_delay_seconds;
}

std::optional<ClassSelectionAudioCue> ClassSelectionMenuModel::take_audio_cue() noexcept {
    auto result = pending_audio_cue_;
    pending_audio_cue_.reset();
    return result;
}

bool ClassSelectionMenuModel::has_scrollbar() const noexcept {
    // SelectClass creates the HorizontalScrollBar only for more than five classes.
    return classes_.size() > scrollbar_visible;
}

ClassScrollbarGeometry ClassSelectionMenuModel::scrollbar_geometry() const noexcept {
    ClassScrollbarGeometry geometry;
    const auto max_scroll =
        classes_.size() > scrollbar_visible ? classes_.size() - scrollbar_visible : 0U;
    geometry.thumb_length = retail_class_thumb_length(classes_.size(), scrollbar_visible);
    geometry.coord_at_min = scrollbar_coord_at_min;
    geometry.coord_at_max =
        scrollbar_coord_at_min + (scrollbar_full_length - geometry.thumb_length);
    if (scroll_float_ <= 0.0 || max_scroll == 0U) {
        geometry.thumb_x = geometry.coord_at_min;
    } else if (scroll_float_ >= static_cast<double>(max_scroll)) {
        geometry.thumb_x = geometry.coord_at_max;
    } else {
        geometry.thumb_x = geometry.coord_at_min +
                           scroll_float_ * (geometry.coord_at_max - geometry.coord_at_min) /
                               static_cast<double>(max_scroll);
    }
    geometry.dec_enabled = scroll_float_ > 0.0;
    geometry.inc_enabled = scroll_float_ < static_cast<double>(max_scroll);
    return geometry;
}

void ClassSelectionMenuModel::set_scroll_int(int value) noexcept {
    const auto max_scroll = static_cast<int>(
        classes_.size() > scrollbar_visible ? classes_.size() - scrollbar_visible : 0U);
    const auto clamped = std::clamp(value, 0, max_scroll);
    scroll_float_ = static_cast<double>(clamped);
    if (static_cast<std::size_t>(clamped) != visible_class_offset_) {
        visible_class_offset_ = static_cast<std::size_t>(clamped);
        // SelectClass.on_scroll: menu_scrollA unless silent.
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
    }
}

void ClassSelectionMenuModel::drag_scroll_to(double x) noexcept {
    // ScrollBar.update_scroll_bar_position: the thumb centre snaps to the
    // cursor, the float position follows smoothly and the list steps only
    // when the set_as_int=False integer mapping changes.
    const auto geometry = scrollbar_geometry();
    const double span = geometry.coord_at_max - geometry.coord_at_min;
    if (span == 0.0 || !has_scrollbar()) return;
    double coord = std::floor(x - geometry.thumb_length / 2.0);
    coord = std::clamp(coord, geometry.coord_at_min, geometry.coord_at_max);
    const auto max_scroll = classes_.size() - scrollbar_visible;
    scroll_float_ = (coord - geometry.coord_at_min) * static_cast<double>(max_scroll) / span;
    const auto index = retail_drag_scroll_index(scroll_float_, max_scroll);
    if (index != visible_class_offset_) {
        visible_class_offset_ = index;
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
    }
}

std::optional<ClassScrollButton>
ClassSelectionMenuModel::hit_scroll_button(ui::Point point) const noexcept {
    if (!has_scrollbar()) return std::nullopt;
    const ClassScrollbarGeometry geometry;
    if (inside(point, geometry.dec_button)) return ClassScrollButton::dec;
    if (inside(point, geometry.inc_button)) return ClassScrollButton::inc;
    return std::nullopt;
}

std::optional<ClassSelectionHover>
ClassSelectionMenuModel::hit_item(ui::Point point) const noexcept {
    const auto offset = visible_class_offset();
    const auto visible = std::min(classes_per_page(), classes_.size() - offset);
    for (std::size_t index{}; index < visible; ++index) {
        if (class_card_bounds(index).contains(point)) {
            return ClassSelectionHover{ClassSelectionHover::Kind::class_card, 0U, offset + index};
        }
    }
    for (std::size_t group{}; group < row_options_.size(); ++group) {
        const auto count = std::min<std::size_t>(row_options_[group].size(), 6U);
        for (std::size_t option{}; option < count; ++option) {
            if (inside(point,
                       loadout_cell_x + static_cast<int>(option) * loadout_cell_step_x,
                       loadout_cell_y + static_cast<int>(group) * loadout_row_step,
                       loadout_cell_size, loadout_cell_size)) {
                return ClassSelectionHover{ClassSelectionHover::Kind::loadout_item, group, option};
            }
        }
    }
    const auto first = construct_page_ * constructs_per_page;
    for (std::size_t cell{}; cell < constructs_per_page; ++cell) {
        const auto index = first + cell;
        if (index >= construct_options_.size()) break;
        const auto column = static_cast<int>(cell % 3U);
        const auto row = static_cast<int>(cell / 3U);
        if (inside(point, construct_cell_x + column * construct_step_x,
                   construct_cell_y + row * construct_step_y, construct_cell_size,
                   construct_cell_size)) {
            return ClassSelectionHover{ClassSelectionHover::Kind::construct, 0U, index};
        }
    }
    return std::nullopt;
}

void ClassSelectionMenuModel::pointer_move(std::optional<ui::Point> point,
                                           Clock::time_point now) {
    hovered_ = point;
    if (dragging_ && point.has_value()) drag_scroll_to(static_cast<double>(point->x));
    const auto item = point.has_value() ? hit_item(*point) : std::nullopt;
    if (item != hovered_item_) {
        // on_mouse_over_*_button: popup_timer = time.time(); popups hidden.
        hovered_item_ = item;
        hover_started_ = now;
    }
}

void ClassSelectionMenuModel::pointer_press(std::optional<ui::Point> point) {
    cancel_pointer();
    if (!point.has_value()) return;
    press_point_ = point;
    if (const auto button = hit_scroll_button(*point); button.has_value()) {
        const auto geometry = scrollbar_geometry();
        if (*button == ClassScrollButton::dec ? geometry.dec_enabled : geometry.inc_enabled) {
            pressed_button_ = button;
        }
        return;
    }
    if (has_scrollbar()) {
        const auto geometry = scrollbar_geometry();
        const double x = static_cast<double>(point->x);
        if (x >= geometry.thumb_x && x <= geometry.thumb_x + geometry.thumb_length &&
            point->y >= scrollbar_bar_top &&
            point->y <= scrollbar_bar_top + scrollbar_bar_thickness) {
            dragging_ = true;
            return;
        }
    }
    if (inside(*point, select_bounds) && select_enabled()) select_pressed_ = true;
    if (in_game_ && inside(*point, in_game_back_bounds)) back_pressed_ = true;
}

std::optional<ClassSelectionAction>
ClassSelectionMenuModel::pointer_release(std::optional<ui::Point> point) {
    const bool was_dragging = std::exchange(dragging_, false);
    const auto button = std::exchange(pressed_button_, std::nullopt);
    const bool had_press = press_point_.has_value();
    std::optional<ClassSelectionAction> result;
    if (point.has_value() && !was_dragging) {
        if (button.has_value()) {
            // SquareButton fires on release while pressed, hovered and enabled.
            if (hit_scroll_button(*point) == button) {
                const auto geometry = scrollbar_geometry();
                if (*button == ClassScrollButton::dec && geometry.dec_enabled) {
                    set_scroll_int(static_cast<int>(visible_class_offset_) - 1);
                } else if (*button == ClassScrollButton::inc && geometry.inc_enabled) {
                    set_scroll_int(static_cast<int>(visible_class_offset_) + 1);
                }
            }
        } else if (has_scrollbar() &&
                   point->x >= static_cast<int>(scrollbar_coord_at_min) &&
                   point->x <= static_cast<int>(scrollbar_coord_at_min + scrollbar_full_length) &&
                   point->y >= scrollbar_bar_top &&
                   point->y <= scrollbar_bar_top + scrollbar_bar_thickness) {
            // A release on the channel jumps the thumb centre to the cursor.
            drag_scroll_to(static_cast<double>(point->x));
        } else if (had_press) {
            result = activate(*point);
        }
    }
    select_pressed_ = false;
    back_pressed_ = false;
    press_point_.reset();
    return result;
}

std::optional<ClassSelectionAction> ClassSelectionMenuModel::click(ui::Point point) {
    pointer_press(point);
    return pointer_release(point);
}

void ClassSelectionMenuModel::cancel_pointer() noexcept {
    dragging_ = false;
    pressed_button_.reset();
    select_pressed_ = false;
    back_pressed_ = false;
    press_point_.reset();
}

std::optional<ClassSelectionAction> ClassSelectionMenuModel::activate(ui::Point point) {
    if ((!in_game_ && inside(point, frontend_navbar_back)) ||
        (in_game_ && inside(point, in_game_back_bounds))) {
        pending_audio_cue_ = ClassSelectionAudioCue::back;
        return ClassSelectionAction::back;
    }
    if (inside(point, select_bounds)) {
        if (!select_enabled()) return std::nullopt;
        pending_audio_cue_ = ClassSelectionAudioCue::confirm;
        return ClassSelectionAction::submit;
    }
    const auto hit = hit_item(point);
    if (!hit.has_value()) {
        if (construct_options_.size() > constructs_per_page) {
            if (inside(point, construct_page_back) && construct_page_ > 0U) {
                --construct_page_;
                pending_audio_cue_ = ClassSelectionAudioCue::scroll;
            } else if (inside(point, construct_page_next) &&
                       construct_page_ + 1U < construct_page_count()) {
                ++construct_page_;
                pending_audio_cue_ = ClassSelectionAudioCue::scroll;
            }
        }
        return std::nullopt;
    }
    switch (hit->kind) {
    case ClassSelectionHover::Kind::class_card:
        select_class(hit->index);
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
        break;
    case ClassSelectionHover::Kind::loadout_item:
        // Single selection: clicking the chosen item changes nothing.
        if (option_indices_[hit->group] != hit->index) {
            option_indices_[hit->group] = hit->index;
            pending_audio_cue_ = ClassSelectionAudioCue::scroll;
        }
        break;
    case ClassSelectionHover::Kind::construct: {
        const auto& name = construct_options_[hit->index];
        const auto found = std::ranges::find(prefabs_, name);
        if (found != prefabs_.end()) {
            // TableSelection(max_sel=3, min_sel=1): the last pick stays.
            if (prefabs_.size() <= 1U) break;
            prefabs_.erase(found);
        } else {
            // At capacity the oldest choice is replaced.
            if (prefabs_.size() >= 3U) prefabs_.erase(prefabs_.begin());
            prefabs_.push_back(name);
        }
        pending_audio_cue_ = ClassSelectionAudioCue::scroll;
        break;
    }
    }
    return std::nullopt;
}

void ClassSelectionMenuModel::class_key_press(std::size_t absolute_index) noexcept {
    if (absolute_index < classes_.size()) held_key_ = absolute_index;
}

void ClassSelectionMenuModel::class_key_release(std::size_t absolute_index) noexcept {
    held_key_.reset();
    if (absolute_index >= classes_.size()) return;
    select_class(absolute_index);
    pending_audio_cue_ = ClassSelectionAudioCue::scroll;
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
    if (direction == 0 || !has_scrollbar())
        return;
    set_scroll_int(static_cast<int>(visible_class_offset_) + (direction > 0 ? 1 : -1));
}

void ClassSelectionMenuModel::cycle_group(std::size_t group, int direction) noexcept {
    if (group >= option_indices_.size() || direction == 0)
        return;
    const auto& options = row_options_[group];
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
    rebuild_options();
    const auto saved = class_loadouts_.find(selected_class());
    if (saved != class_loadouts_.end()) {
        option_indices_ = saved->second.options;
        prefabs_ = saved->second.prefabs;
    } else {
        reset_loadout();
    }
    hovered_item_.reset();
}

void ClassSelectionMenuModel::reveal_selected_class() noexcept {
    const auto per_page = classes_per_page();
    if (classes_.size() <= per_page) {
        visible_class_offset_ = 0U;
        scroll_float_ = 0.0;
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
    // set_scroll(min_index, silent=True): the thumb follows without a cue.
    scroll_float_ = static_cast<double>(visible_class_offset_);
}

} // namespace battlespades::frontend
