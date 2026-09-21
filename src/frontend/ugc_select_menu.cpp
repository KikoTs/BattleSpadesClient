#include "battlespades/frontend/ugc_select_menu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>

namespace battlespades::frontend {
namespace {

using ui::Rect;
using ui::Widget;
using ui::WidgetId;

constexpr std::int32_t scale{MainMenuModel::subpixels_per_pixel};

[[nodiscard]] constexpr Rect retail_top_edge(std::int32_t x,
                                             std::int32_t top_y,
                                             std::int32_t width,
                                             std::int32_t height) noexcept {
    // Retail TextButton stores `y` at the top edge in its bottom-left canvas.
    return Rect{x * scale,
                (MainMenuModel::reference_height_pixels - top_y) * scale,
                width * scale,
                height * scale};
}

[[nodiscard]] constexpr Rect retail_bottom_left(std::int32_t x,
                                                std::int32_t y,
                                                std::int32_t width,
                                                std::int32_t height) noexcept {
    return Rect{x * scale,
                (MainMenuModel::reference_height_pixels - y - height) * scale,
                width * scale,
                height * scale};
}

[[nodiscard]] constexpr Widget widget(std::uint32_t id, Rect bounds) noexcept {
    return Widget{WidgetId{id}, bounds, {}};
}

[[nodiscard]] bool retail_hit_test(Rect bounds, ui::Point point) noexcept {
    const auto left = static_cast<std::int64_t>(bounds.x);
    const auto top = static_cast<std::int64_t>(bounds.y);
    const auto right = left + static_cast<std::int64_t>(bounds.width);
    const auto bottom = top + static_cast<std::int64_t>(bounds.height);
    const auto x = static_cast<std::int64_t>(point.x);
    const auto y = static_cast<std::int64_t>(point.y);

    // `collides()` in the retail GUI excludes all four rectangle edges.
    return x > left && x < right && y > top && y < bottom;
}

} // namespace

UgcSelectMenuModel::UgcSelectMenuModel()
    : controls_{
          UgcSelectControl{widget(1U, retail_top_edge(269, 434, 262, 58)),
                           UgcSelectAction::create_map,
                           UgcSelectControlKind::text_button,
                           "UGC_MENU_MAP_EDITOR",
                           {}},
          UgcSelectControl{widget(2U, retail_top_edge(269, 371, 262, 58)),
                           UgcSelectAction::publish_map,
                           UgcSelectControlKind::text_button,
                           "UGC_MENU_PUBLISH_MAP",
                           {}},
          UgcSelectControl{widget(3U, retail_top_edge(269, 308, 262, 58)),
                           UgcSelectAction::subscribe_workshop,
                           UgcSelectControlKind::text_button,
                           "UGC_MENU_SUBSCRIBE",
                           {}},
          // Spades 24 px measures BACK at 47 px; +5 px pad +25 px icon = 78 px.
          UgcSelectControl{widget(4U, retail_bottom_left(248, 32, 78, 26)),
                           UgcSelectAction::back,
                           UgcSelectControlKind::navigation_item,
                           "BACK",
                           ugc_select_assets::back_icon},
      } {
    rebuild_focus();
}

std::span<const UgcSelectControl> UgcSelectMenuModel::controls() const noexcept {
    return controls_;
}

std::optional<WidgetId> UgcSelectMenuModel::hovered() const noexcept {
    return hovered_;
}

std::optional<WidgetId> UgcSelectMenuModel::focused() const noexcept {
    return focus_.focused();
}

bool UgcSelectMenuModel::invalid_data_error() const noexcept {
    return invalid_data_error_;
}

std::uint32_t UgcSelectMenuModel::steam_app_id() const noexcept {
    return steam_app_id_;
}

std::string UgcSelectMenuModel::workshop_url() const {
    return "https://www.aosplay.net/workshop";
}

bool UgcSelectMenuModel::set_steam_app_id(std::uint32_t app_id) noexcept {
    if (app_id == 0U) {
        return false;
    }
    steam_app_id_ = app_id;
    return true;
}

void UgcSelectMenuModel::set_invalid_data_error(bool invalid) noexcept {
    invalid_data_error_ = invalid;
    for (std::size_t index = 0U; index < 3U; ++index) {
        controls_[index].widget.state.enabled = !invalid;
    }
    // Rebuilding repairs focus immediately if the focused route was disabled.
    rebuild_focus();
}

void UgcSelectMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_test(*point) : std::nullopt;
}

void UgcSelectMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    pointer_down_ = true;
    navigation_item_armed_ = false;
    if (hovered_.has_value()) {
        const auto index = index_of(*hovered_);
        navigation_item_armed_ =
            index.has_value() &&
            controls_[*index].kind == UgcSelectControlKind::navigation_item;
    }
}

std::optional<UgcSelectActivation>
UgcSelectMenuModel::pointer_release(std::optional<ui::Point> point) {
    pointer_move(point);
    std::optional<UgcSelectActivation> result;
    if (pointer_down_ && hovered_.has_value()) {
        const auto index = index_of(*hovered_);
        if (index.has_value()) {
            const auto& control = controls_[*index];
            if (control.widget.state.enabled && control.widget.state.visible &&
                (control.kind != UgcSelectControlKind::navigation_item ||
                 navigation_item_armed_)) {
                result = activation_for(control.action);
                static_cast<void>(focus_.set_focused(control.widget.id));
            }
        }
    }
    pointer_down_ = false;
    navigation_item_armed_ = false;
    return result;
}

std::optional<UgcSelectActivation> UgcSelectMenuModel::handle(ui::InputEvent event) {
    if (!event.triggers_action()) {
        return std::nullopt;
    }

    using ui::FocusDirection;
    using ui::InputAction;
    switch (event.action) {
    case InputAction::navigate_up:
        static_cast<void>(focus_.move(FocusDirection::up));
        break;
    case InputAction::navigate_down:
        static_cast<void>(focus_.move(FocusDirection::down));
        break;
    case InputAction::navigate_left:
        static_cast<void>(focus_.move(FocusDirection::left));
        break;
    case InputAction::navigate_right:
        static_cast<void>(focus_.move(FocusDirection::right));
        break;
    case InputAction::focus_next:
        static_cast<void>(focus_.advance());
        break;
    case InputAction::focus_previous:
        static_cast<void>(focus_.advance(true));
        break;
    case InputAction::activate:
        return focused_activation();
    case InputAction::cancel:
        return activation_for(UgcSelectAction::back);
    }
    return std::nullopt;
}

WidgetVisualState UgcSelectMenuModel::visual_state(WidgetId id) const noexcept {
    const auto index = index_of(id);
    if (!index.has_value()) {
        return WidgetVisualState::disabled;
    }

    const auto& control = controls_[*index];
    if (!control.widget.state.enabled || !control.widget.state.visible) {
        return WidgetVisualState::disabled;
    }
    if (pointer_down_ && hovered_ == id &&
        (control.kind != UgcSelectControlKind::navigation_item || navigation_item_armed_)) {
        return WidgetVisualState::pressed;
    }
    if (hovered_ == id) {
        return WidgetVisualState::hovered;
    }
    if (focus_.focused() == id) {
        return WidgetVisualState::focused;
    }
    return WidgetVisualState::normal;
}

std::optional<std::size_t> UgcSelectMenuModel::index_of(WidgetId id) const noexcept {
    const auto iterator = std::ranges::find(
        controls_, id, [](const UgcSelectControl& control) { return control.widget.id; });
    if (iterator == controls_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(controls_.begin(), iterator));
}

std::optional<WidgetId> UgcSelectMenuModel::hit_test(ui::Point point) const noexcept {
    for (auto iterator = controls_.rbegin(); iterator != controls_.rend(); ++iterator) {
        if (iterator->widget.state.visible && retail_hit_test(iterator->widget.bounds, point)) {
            return iterator->widget.id;
        }
    }
    return std::nullopt;
}

std::optional<UgcSelectActivation> UgcSelectMenuModel::focused_activation() const {
    if (!focus_.focused().has_value()) {
        return std::nullopt;
    }
    const auto index = index_of(*focus_.focused());
    if (!index.has_value() || !controls_[*index].widget.state.enabled ||
        !controls_[*index].widget.state.visible) {
        return std::nullopt;
    }
    return activation_for(controls_[*index].action);
}

UgcSelectActivation UgcSelectMenuModel::activation_for(UgcSelectAction action) const {
    const auto sound = action == UgcSelectAction::back ? main_menu_assets::back_sound
                                                       : main_menu_assets::confirmation_sound;
    if (action == UgcSelectAction::subscribe_workshop) {
        return UgcSelectActivation{action, sound, workshop_url()};
    }
    return UgcSelectActivation{action, sound, std::nullopt};
}

void UgcSelectMenuModel::rebuild_focus() noexcept {
    std::array<Widget, control_count> widgets{};
    std::ranges::transform(
        controls_, widgets.begin(), [](const UgcSelectControl& control) { return control.widget; });
    static_cast<void>(focus_.set_widgets(widgets));
}

namespace ugc_select_assets {

std::span<const MainMenuAsset> required() noexcept {
    constexpr auto texture = MainMenuAssetKind::texture;
    constexpr auto font = MainMenuAssetKind::font;
    constexpr auto music = MainMenuAssetKind::music;
    constexpr auto sound = MainMenuAssetKind::sound;
    constexpr auto linear = TextureSampling::linear;
    constexpr auto nearest = TextureSampling::nearest;
    constexpr auto none = TextureSampling::not_applicable;
    constexpr auto top_left = TextureAnchor::top_left;
    constexpr auto center = TextureAnchor::center;
    constexpr auto no_anchor = TextureAnchor::not_applicable;

    static constexpr std::array assets{
        MainMenuAsset{main_menu_assets::background, texture, linear, top_left, 0.6},
        MainMenuAsset{main_menu_assets::splash, texture, linear, center, 0.6},
        MainMenuAsset{three_button_frame, texture, linear, center, 0.6},
        MainMenuAsset{small_navigation_frame, texture, linear, center, 0.6},
        MainMenuAsset{main_menu_assets::button_left, texture, nearest, top_left, 0.6},
        MainMenuAsset{main_menu_assets::button_middle, texture, nearest, top_left, 0.6},
        MainMenuAsset{main_menu_assets::button_right, texture, nearest, top_left, 0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_left.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_mid.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_right.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_left.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_mid.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_right.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{back_icon, texture, linear, center, 0.64},
        MainMenuAsset{main_menu_assets::button_font, font, none, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::music, music, none, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::confirmation_sound, sound, none, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::back_sound, sound, none, no_anchor, 1.0},
    };
    return assets;
}

} // namespace ugc_select_assets

} // namespace battlespades::frontend
