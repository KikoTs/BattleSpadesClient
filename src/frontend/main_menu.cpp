#include "battlespades/frontend/main_menu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

namespace battlespades::frontend {
namespace {

using ui::Rect;
using ui::Widget;
using ui::WidgetId;

constexpr std::int32_t scale{MainMenuModel::subpixels_per_pixel};
constexpr std::int32_t canvas_height{MainMenuModel::reference_height_pixels * scale};

[[nodiscard]] constexpr Rect retail_text_button(std::int32_t x,
                                                std::int32_t top_y,
                                                std::int32_t width,
                                                std::int32_t height) noexcept {
    return Rect{
        x * scale,
        (MainMenuModel::reference_height_pixels - top_y) * scale,
        width * scale,
        height * scale,
    };
}

[[nodiscard]] constexpr Rect retail_centered_square(std::int32_t center_x_eighths,
                                                    std::int32_t center_y_eighths,
                                                    std::int32_t size_eighths) noexcept {
    const auto half_size = size_eighths / 2;
    return Rect{
        center_x_eighths - half_size,
        canvas_height - center_y_eighths - half_size,
        size_eighths,
        size_eighths,
    };
}

[[nodiscard]] constexpr Widget menu_widget(std::uint32_t id, Rect bounds) noexcept {
    return Widget{WidgetId{id}, bounds, {}};
}

[[nodiscard]] bool retail_hit_test(const Rect& bounds, ui::Point point) noexcept {
    const auto left = static_cast<std::int64_t>(bounds.x);
    const auto top = static_cast<std::int64_t>(bounds.y);
    const auto right = left + static_cast<std::int64_t>(bounds.width);
    const auto bottom = top + static_cast<std::int64_t>(bounds.height);
    const auto x = static_cast<std::int64_t>(point.x);
    const auto y = static_cast<std::int64_t>(point.y);

    // The recovered collides(rect, point) helper excludes all four edges.
    return x > left && x < right && y > top && y < bottom;
}

constexpr std::int32_t square_size{510};             // 63.75 retail pixels.
constexpr std::int32_t first_square_center_x{2'391}; // 298.875 pixels.
constexpr std::int32_t square_center_y{985};         // 123.125 pixels.
constexpr std::int32_t square_stride{542};           // 67.75 pixels.
constexpr std::int32_t quit_item_width{584};         // 73 pixels at retail font metrics.

constexpr std::array<std::string_view, 4U> square_icons{
    "png/ui/icons/icon_tutorial.png",
    "png/ui/icons/icon_friends.png",
    "png/ui/icons/icon_leaderboards.png",
    "png/ui/icons/icon_options.png",
};

} // namespace

MainMenuModel::MainMenuModel()
    : controls_{
          MainMenuControl{
              menu_widget(
                  1U, retail_centered_square(first_square_center_x, square_center_y, square_size)),
              MainMenuAction::tutorial,
              MainMenuControlKind::square_button,
              "TUTORIAL",
              square_icons[0],
          },
          MainMenuControl{
              menu_widget(2U,
                          retail_centered_square(
                              first_square_center_x + square_stride, square_center_y, square_size)),
              MainMenuAction::friends,
              MainMenuControlKind::square_button,
              "FRIENDS",
              square_icons[1],
          },
          MainMenuControl{
              menu_widget(3U,
                          retail_centered_square(first_square_center_x + square_stride * 2,
                                                 square_center_y,
                                                 square_size)),
              MainMenuAction::leaderboard,
              MainMenuControlKind::square_button,
              "LEADERBOARD",
              square_icons[2],
          },
          MainMenuControl{
              menu_widget(4U,
                          retail_centered_square(first_square_center_x + square_stride * 3,
                                                 square_center_y,
                                                 square_size)),
              MainMenuAction::settings,
              MainMenuControlKind::square_button,
              "SETTINGS",
              square_icons[3],
          },
          MainMenuControl{
              menu_widget(5U, retail_text_button(269, 232, 262, 58)),
              MainMenuAction::user_content,
              MainMenuControlKind::text_button,
              "UGC_MAIN_MENU_UGC_BUTTON",
              {},
          },
          MainMenuControl{
              menu_widget(6U, retail_text_button(269, 295, 262, 58)),
              MainMenuAction::player_profile,
              MainMenuControlKind::text_button,
              "INVENTORY",
              {},
          },
          MainMenuControl{
              menu_widget(7U, retail_text_button(269, 358, 262, 58)),
              MainMenuAction::create_match,
              MainMenuControlKind::text_button,
              "CREATE_MATCH",
              {},
          },
          MainMenuControl{
              menu_widget(8U, retail_text_button(269, 421, 262, 58)),
              MainMenuAction::join_match,
              MainMenuControlKind::text_button,
              "JOIN_MATCH",
              {},
          },
          MainMenuControl{
              menu_widget(9U,
                          Rect{
                              (400 * scale) - quit_item_width / 2,
                              (reference_height_pixels - 32 - 26) * scale,
                              quit_item_width,
                              26 * scale,
                          }),
              MainMenuAction::quit,
              MainMenuControlKind::navigation_item,
              "QUIT",
              "png/ui/common_elements/nav_bar/quit_icon.png",
          },
          MainMenuControl{
              menu_widget(10U,
                          Rect{
                              680 * scale,
                              (reference_height_pixels - 32 - 26) * scale,
                              110 * scale,
                              26 * scale,
                          }),
              MainMenuAction::logout,
              MainMenuControlKind::navigation_item,
              "LOGOUT",
              "png/ui/common_elements/nav_bar/back_icon.png",
          },
      } {
    static_cast<void>(rebuild_focus());
}

std::span<const MainMenuControl> MainMenuModel::controls() const noexcept {
    return controls_;
}

std::optional<WidgetId> MainMenuModel::hovered() const noexcept {
    return hovered_;
}

std::optional<WidgetId> MainMenuModel::focused() const noexcept {
    return focus_.focused();
}

void MainMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_test(*point) : std::nullopt;
}

void MainMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    pointer_down_ = true;
    navigation_item_armed_ = false;
    if (hovered_.has_value()) {
        const auto index = index_of(*hovered_);
        navigation_item_armed_ =
            index.has_value() && controls_[*index].kind == MainMenuControlKind::navigation_item;
    }
}

std::optional<MainMenuAction>
MainMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    pointer_move(point);

    std::optional<MainMenuAction> result;
    if (pointer_down_ && hovered_.has_value()) {
        const auto index = index_of(*hovered_);
        if (index.has_value() && controls_[*index].widget.state.enabled &&
            controls_[*index].widget.state.visible &&
            (controls_[*index].kind != MainMenuControlKind::navigation_item ||
             navigation_item_armed_)) {
            result = controls_[*index].action;
            static_cast<void>(focus_.set_focused(*hovered_));
        }
    }

    pointer_down_ = false;
    navigation_item_armed_ = false;
    return result;
}

std::optional<MainMenuAction> MainMenuModel::handle(ui::InputEvent event) noexcept {
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
        return focused_action();
    case InputAction::cancel:
        break;
    }

    return std::nullopt;
}

bool MainMenuModel::set_enabled(MainMenuAction action, bool enabled) noexcept {
    const auto index = index_of(action);
    if (!index.has_value()) {
        return false;
    }

    auto state = controls_[*index].widget.state;
    state.enabled = enabled;
    controls_[*index].widget.state = state;
    if (!focus_.set_widget_state(controls_[*index].widget.id, state)) {
        return false;
    }
    return true;
}

WidgetVisualState MainMenuModel::visual_state(WidgetId id) const noexcept {
    const auto index = index_of(id);
    if (!index.has_value()) {
        return WidgetVisualState::disabled;
    }

    const auto& control = controls_[*index];
    if (!control.widget.state.enabled || !control.widget.state.visible) {
        return WidgetVisualState::disabled;
    }
    if (pointer_down_ && hovered_ == id &&
        (control.kind != MainMenuControlKind::navigation_item || navigation_item_armed_)) {
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

std::optional<std::size_t> MainMenuModel::index_of(WidgetId id) const noexcept {
    const auto iterator = std::ranges::find(
        controls_, id, [](const MainMenuControl& control) { return control.widget.id; });
    if (iterator == controls_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(controls_.begin(), iterator));
}

std::optional<std::size_t> MainMenuModel::index_of(MainMenuAction action) const noexcept {
    const auto iterator = std::ranges::find(controls_, action, &MainMenuControl::action);
    if (iterator == controls_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(controls_.begin(), iterator));
}

std::optional<WidgetId> MainMenuModel::hit_test(ui::Point point) const noexcept {
    for (auto iterator = controls_.rbegin(); iterator != controls_.rend(); ++iterator) {
        if (iterator->widget.state.visible && retail_hit_test(iterator->widget.bounds, point)) {
            return iterator->widget.id;
        }
    }
    return std::nullopt;
}

std::optional<MainMenuAction> MainMenuModel::focused_action() const noexcept {
    if (!focus_.focused().has_value()) {
        return std::nullopt;
    }

    const auto index = index_of(*focus_.focused());
    if (!index.has_value() || !controls_[*index].widget.state.enabled ||
        !controls_[*index].widget.state.visible) {
        return std::nullopt;
    }
    return controls_[*index].action;
}

bool MainMenuModel::rebuild_focus() noexcept {
    std::array<ui::Widget, control_count> widgets{};
    std::ranges::transform(
        controls_, widgets.begin(), [](const MainMenuControl& control) { return control.widget; });
    return focus_.set_widgets(widgets);
}

namespace main_menu_assets {

std::span<const MainMenuAsset> required() noexcept {
    constexpr auto texture_asset = MainMenuAssetKind::texture;
    constexpr auto font_asset = MainMenuAssetKind::font;
    constexpr auto music_asset = MainMenuAssetKind::music;
    constexpr auto sound_asset = MainMenuAssetKind::sound;
    constexpr auto linear_filter = TextureSampling::linear;
    constexpr auto nearest_filter = TextureSampling::nearest;
    constexpr auto no_filter = TextureSampling::not_applicable;
    constexpr auto top_left_anchor = TextureAnchor::top_left;
    constexpr auto center_anchor = TextureAnchor::center;
    constexpr auto no_anchor = TextureAnchor::not_applicable;

    static constexpr std::array assets{
        MainMenuAsset{background, texture_asset, linear_filter, top_left_anchor, 0.6},
        MainMenuAsset{splash, texture_asset, linear_filter, center_anchor, 0.6},
        MainMenuAsset{frame, texture_asset, linear_filter, center_anchor, 0.6},
        MainMenuAsset{
            player_name_frame,
            texture_asset,
            linear_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            button_left,
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            button_middle,
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            button_right,
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_large_hover_left.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_large_hover_mid.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_large_hover_right.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_large_press_left.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_large_press_mid.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_large_press_right.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            0.6,
        },
        MainMenuAsset{
            square_button,
            texture_asset,
            nearest_filter,
            top_left_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/mm_button_square_hover.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/common_elements/buttons/mm_button_square_press.png",
            texture_asset,
            nearest_filter,
            top_left_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/icons/icon_tutorial.png",
            texture_asset,
            nearest_filter,
            center_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/icons/icon_friends.png",
            texture_asset,
            nearest_filter,
            center_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/icons/icon_leaderboards.png",
            texture_asset,
            nearest_filter,
            center_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/icons/icon_options.png",
            texture_asset,
            nearest_filter,
            center_anchor,
            1.0,
        },
        MainMenuAsset{
            "png/ui/common_elements/nav_bar/quit_icon.png",
            texture_asset,
            linear_filter,
            center_anchor,
            0.64,
        },
        MainMenuAsset{
            "png/ui/common_elements/nav_bar/back_icon.png",
            texture_asset,
            linear_filter,
            center_anchor,
            0.64,
        },
        MainMenuAsset{welcome_font, font_asset, no_filter, no_anchor, 1.0},
        MainMenuAsset{button_font, font_asset, no_filter, no_anchor, 1.0},
        MainMenuAsset{music, music_asset, no_filter, no_anchor, 1.0},
        MainMenuAsset{
            confirmation_sound,
            sound_asset,
            no_filter,
            no_anchor,
            1.0,
        },
    };
    return assets;
}

} // namespace main_menu_assets

} // namespace battlespades::frontend
