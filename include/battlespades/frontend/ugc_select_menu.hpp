#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/focus_navigator.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/** User intentions exposed by the retail UGC Select scene. */
enum class UgcSelectAction : std::uint8_t {
    create_map,
    publish_map,
    subscribe_workshop,
    back,
};

enum class UgcSelectControlKind : std::uint8_t {
    text_button,
    navigation_item,
};

/** Static widget metadata recovered from `ugcSelectMenu.py`. */
struct UgcSelectControl final {
    ui::Widget widget{};
    UgcSelectAction action{UgcSelectAction::create_map};
    UgcSelectControlKind kind{UgcSelectControlKind::text_button};
    std::string_view localization_key;
    std::string_view icon_asset;
};

/**
 * One side-effect request emitted after a successful activation.
 *
 * Routing, audio playback, and web-overlay ownership stay outside the model.
 * `external_url` is populated only for Subscribe; the other actions are local
 * frontend routes. The string is owned so changing the configured app id can
 * never invalidate a command already queued by the application.
 */
struct UgcSelectActivation final {
    UgcSelectAction action{UgcSelectAction::create_map};
    std::string_view sound_asset;
    std::optional<std::string> external_url;
};

/** Renderer-neutral reconstruction of the retail three-button UGC scene. */
class UgcSelectMenuModel final {
public:
    static constexpr std::uint32_t retail_steam_app_id{224'540U};
    static constexpr std::size_t control_count{4U};

    UgcSelectMenuModel();

    [[nodiscard]] std::span<const UgcSelectControl> controls() const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hovered() const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> focused() const noexcept;
    [[nodiscard]] bool invalid_data_error() const noexcept;
    [[nodiscard]] std::uint32_t steam_app_id() const noexcept;
    [[nodiscard]] std::string workshop_url() const;

    /** Zero is rejected so Subscribe can never emit an unusable app-id URL. */
    [[nodiscard]] bool set_steam_app_id(std::uint32_t app_id) noexcept;

    /**
     * Mirrors `GameManager.invalid_data_error`: all UGC operations become
     * unavailable while Back remains active, allowing recovery to Select Menu.
     */
    void set_invalid_data_error(bool invalid) noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<UgcSelectActivation>
    pointer_release(std::optional<ui::Point> point);
    [[nodiscard]] std::optional<UgcSelectActivation> handle(ui::InputEvent event);
    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> index_of(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hit_test(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<UgcSelectActivation> focused_activation() const;
    [[nodiscard]] UgcSelectActivation activation_for(UgcSelectAction action) const;
    void rebuild_focus() noexcept;

    std::array<UgcSelectControl, control_count> controls_{};
    ui::FocusNavigator focus_{};
    std::optional<ui::WidgetId> hovered_{};
    std::uint32_t steam_app_id_{retail_steam_app_id};
    bool invalid_data_error_{};
    bool pointer_down_{};
    bool navigation_item_armed_{};
};

namespace ugc_select_assets {

inline constexpr std::string_view three_button_frame{
    "png/ui/main_menu/frame_3button_menu.png"};
inline constexpr std::string_view small_navigation_frame{
    "png/ui/main_menu/frame_nav_bar_small.png"};
inline constexpr std::string_view back_icon{
    "png/ui/common_elements/nav_bar/back_icon.png"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace ugc_select_assets

} // namespace battlespades::frontend
