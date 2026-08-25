#include "battlespades/frontend/parity_debug_menu.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace battlespades::frontend {
namespace {

constexpr ui::Rect filter_bounds{242, 114, 130, 24};
constexpr ui::Rect first_row_bounds{66, 155, 320, 25};
constexpr ui::Rect back_bounds{54, 541, 105, 32};
constexpr ui::Rect open_bounds{405, 456, 162, 50};
constexpr ui::Rect hitbox_bounds{575, 456, 162, 50};

[[nodiscard]] constexpr bool matches(ParityCatalogKind kind,
                                     ParityDebugFilter filter) noexcept {
    switch (filter) {
    case ParityDebugFilter::all: return true;
    case ParityDebugFilter::screens: return kind == ParityCatalogKind::screen;
    case ParityDebugFilter::components: return kind == ParityCatalogKind::component;
    case ParityDebugFilter::widgets: return kind == ParityCatalogKind::widget;
    case ParityDebugFilter::services: return kind == ParityCatalogKind::service;
    }
    return false;
}

[[nodiscard]] constexpr ParityDebugFilter cycled_filter(ParityDebugFilter value,
                                                        std::int32_t direction) noexcept {
    constexpr std::int32_t count{5};
    const auto current = static_cast<std::int32_t>(value);
    const auto offset = direction < 0 ? -1 : 1;
    return static_cast<ParityDebugFilter>((current + offset + count) % count);
}

} // namespace

ParityDebugMenuModel::ParityDebugMenuModel() {
    rebuild_filter();
}

ParityDebugFilter ParityDebugMenuModel::filter() const noexcept {
    return filter_;
}

std::string_view ParityDebugMenuModel::filter_label() const noexcept {
    switch (filter_) {
    case ParityDebugFilter::all: return "ALL";
    case ParityDebugFilter::screens: return "SCREENS";
    case ParityDebugFilter::components: return "COMPONENTS";
    case ParityDebugFilter::widgets: return "WIDGETS";
    case ParityDebugFilter::services: return "SERVICES";
    }
    return "ALL";
}

std::span<const std::size_t> ParityDebugMenuModel::filtered_indices() const noexcept {
    return filtered_indices_;
}

std::size_t ParityDebugMenuModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

std::optional<std::size_t> ParityDebugMenuModel::selected_filtered_row() const noexcept {
    return selected_row_;
}

const ParityCatalogEntry* ParityDebugMenuModel::selected_entry() const noexcept {
    if (!selected_row_.has_value() || *selected_row_ >= filtered_indices_.size()) {
        return nullptr;
    }
    const auto catalog = retail_frontend_catalog();
    const auto index = filtered_indices_[*selected_row_];
    return index < catalog.size() ? &catalog[index] : nullptr;
}

bool ParityDebugMenuModel::show_hitboxes() const noexcept {
    return show_hitboxes_;
}

std::size_t ParityDebugMenuModel::detail_first_row() const noexcept {
    return detail_first_row_;
}

std::size_t ParityDebugMenuModel::detail_line_count() const noexcept {
    const auto* entry = selected_entry();
    if (entry == nullptr) {
        return 0U;
    }
    constexpr std::size_t metadata_lines{6U};
    return metadata_lines + entry->widgets.size() + entry->assets.size() +
           entry->substates.size();
}

void ParityDebugMenuModel::cycle_filter(std::int32_t direction) {
    if (direction == 0) {
        return;
    }
    filter_ = cycled_filter(filter_, direction);
    rebuild_filter();
}

bool ParityDebugMenuModel::select_filtered_row(std::size_t row) noexcept {
    if (row >= filtered_indices_.size()) {
        return false;
    }
    selected_row_ = row;
    detail_first_row_ = 0U;
    reveal_selection();
    return true;
}

bool ParityDebugMenuModel::select_visible_row(std::size_t row) noexcept {
    if (row >= visible_rows) {
        return false;
    }
    return select_filtered_row(first_visible_row_ + row);
}

void ParityDebugMenuModel::scroll_rows(std::int32_t delta) noexcept {
    if (delta == 0 || filtered_indices_.size() <= visible_rows) {
        return;
    }
    const auto maximum = filtered_indices_.size() - visible_rows;
    if (delta < 0) {
        const auto amount = static_cast<std::size_t>(-(static_cast<std::int64_t>(delta)));
        first_visible_row_ = amount > first_visible_row_ ? 0U : first_visible_row_ - amount;
    } else {
        const auto amount = static_cast<std::size_t>(delta);
        first_visible_row_ = std::min(maximum, first_visible_row_ + amount);
    }
}

void ParityDebugMenuModel::scroll_details(std::int32_t delta) noexcept {
    constexpr std::size_t visible_detail_rows{11U};
    const auto count = detail_line_count();
    if (delta == 0 || count <= visible_detail_rows) {
        return;
    }
    const auto maximum = count - visible_detail_rows;
    if (delta < 0) {
        const auto amount = static_cast<std::size_t>(-(static_cast<std::int64_t>(delta)));
        detail_first_row_ = amount > detail_first_row_ ? 0U : detail_first_row_ - amount;
    } else {
        detail_first_row_ = std::min(maximum, detail_first_row_ +
                                                  static_cast<std::size_t>(delta));
    }
}

void ParityDebugMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_visible_row_.reset();
    filter_hovered_ = false;
    open_hovered_ = false;
    hitbox_hovered_ = false;
    if (!point.has_value()) {
        return;
    }
    hovered_visible_row_ = hit_visible_row(*point);
    filter_hovered_ = filter_bounds.contains(*point);
    open_hovered_ = open_bounds.contains(*point) && selected_entry() != nullptr &&
                    selected_entry()->native_fixture;
    hitbox_hovered_ = hitbox_bounds.contains(*point);
}

void ParityDebugMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_down_ = point.has_value();
    pointer_move(point);
}

std::optional<ParityDebugActivation>
ParityDebugMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    const bool armed = pointer_down_;
    pointer_down_ = false;
    if (!armed || !point.has_value()) {
        pointer_move(point);
        return std::nullopt;
    }
    auto result = activate_at(*point);
    pointer_move(point);
    return result;
}

std::optional<ParityDebugActivation> ParityDebugMenuModel::handle(ui::InputEvent event) {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    switch (event.action) {
    case ui::InputAction::navigate_left:
        cycle_filter(-1);
        return std::nullopt;
    case ui::InputAction::navigate_right:
        cycle_filter(1);
        return std::nullopt;
    case ui::InputAction::navigate_up:
    case ui::InputAction::focus_previous:
        if (!selected_row_.has_value()) {
            return std::nullopt;
        }
        static_cast<void>(select_filtered_row(*selected_row_ == 0U
                                                  ? filtered_indices_.size() - 1U
                                                  : *selected_row_ - 1U));
        return std::nullopt;
    case ui::InputAction::navigate_down:
    case ui::InputAction::focus_next:
        if (!selected_row_.has_value()) {
            return std::nullopt;
        }
        static_cast<void>(select_filtered_row((*selected_row_ + 1U) %
                                              filtered_indices_.size()));
        return std::nullopt;
    case ui::InputAction::activate: {
        const auto* entry = selected_entry();
        if (entry != nullptr && entry->native_fixture) {
            return ParityDebugActivation{ParityDebugAction::open_native_fixture, entry->id};
        }
        return std::nullopt;
    }
    case ui::InputAction::cancel:
        return ParityDebugActivation{ParityDebugAction::back, {}};
    }
    return std::nullopt;
}

std::optional<std::size_t> ParityDebugMenuModel::hovered_visible_row() const noexcept {
    return hovered_visible_row_;
}

bool ParityDebugMenuModel::filter_hovered() const noexcept {
    return filter_hovered_;
}

bool ParityDebugMenuModel::open_hovered() const noexcept {
    return open_hovered_;
}

bool ParityDebugMenuModel::hitbox_hovered() const noexcept {
    return hitbox_hovered_;
}

void ParityDebugMenuModel::rebuild_filter() {
    const auto catalog = retail_frontend_catalog();
    std::optional<std::string> selected_id;
    if (const auto* selected = selected_entry(); selected != nullptr) {
        selected_id = selected->id;
    }

    filtered_indices_.clear();
    filtered_indices_.reserve(catalog.size());
    for (std::size_t index{}; index < catalog.size(); ++index) {
        if (matches(catalog[index].kind, filter_)) {
            filtered_indices_.push_back(index);
        }
    }
    first_visible_row_ = 0U;
    detail_first_row_ = 0U;
    selected_row_.reset();
    if (selected_id.has_value()) {
        const auto found = std::ranges::find_if(filtered_indices_, [&](std::size_t index) {
            return catalog[index].id == *selected_id;
        });
        if (found != filtered_indices_.end()) {
            selected_row_ = static_cast<std::size_t>(found - filtered_indices_.begin());
        }
    }
    if (!selected_row_.has_value() && !filtered_indices_.empty()) {
        selected_row_ = 0U;
    }
    reveal_selection();
}

void ParityDebugMenuModel::reveal_selection() noexcept {
    if (!selected_row_.has_value() || filtered_indices_.size() <= visible_rows) {
        first_visible_row_ = 0U;
        return;
    }
    if (*selected_row_ < first_visible_row_) {
        first_visible_row_ = *selected_row_;
    } else if (*selected_row_ >= first_visible_row_ + visible_rows) {
        first_visible_row_ = *selected_row_ - visible_rows + 1U;
    }
    first_visible_row_ =
        std::min(first_visible_row_, filtered_indices_.size() - visible_rows);
}

std::optional<std::size_t>
ParityDebugMenuModel::hit_visible_row(ui::Point point) const noexcept {
    if (point.x < first_row_bounds.x ||
        point.x >= first_row_bounds.x + first_row_bounds.width ||
        point.y < first_row_bounds.y) {
        return std::nullopt;
    }
    const auto row = static_cast<std::size_t>((point.y - first_row_bounds.y) /
                                              first_row_bounds.height);
    if (row >= visible_rows || first_visible_row_ + row >= filtered_indices_.size()) {
        return std::nullopt;
    }
    return row;
}

std::optional<ParityDebugActivation> ParityDebugMenuModel::activate_at(ui::Point point) {
    if (back_bounds.contains(point)) {
        return ParityDebugActivation{ParityDebugAction::back, {}};
    }
    if (filter_bounds.contains(point)) {
        cycle_filter(1);
        return std::nullopt;
    }
    if (const auto row = hit_visible_row(point); row.has_value()) {
        static_cast<void>(select_visible_row(*row));
        return std::nullopt;
    }
    if (hitbox_bounds.contains(point)) {
        show_hitboxes_ = !show_hitboxes_;
        return ParityDebugActivation{ParityDebugAction::toggle_hitboxes, {}};
    }
    if (open_bounds.contains(point)) {
        const auto* entry = selected_entry();
        if (entry != nullptr && entry->native_fixture) {
            return ParityDebugActivation{ParityDebugAction::open_native_fixture, entry->id};
        }
    }
    return std::nullopt;
}

std::string_view parity_kind_label(ParityCatalogKind kind) noexcept {
    switch (kind) {
    case ParityCatalogKind::screen: return "SCREEN";
    case ParityCatalogKind::component: return "COMPONENT";
    case ParityCatalogKind::widget: return "WIDGET";
    case ParityCatalogKind::service: return "SERVICE/DATA";
    }
    return "UNKNOWN";
}

} // namespace battlespades::frontend
