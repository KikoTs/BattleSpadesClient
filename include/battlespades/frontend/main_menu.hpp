#pragma once

#include "battlespades/ui/focus_navigator.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::frontend {

/**
 * Typed intentions emitted by the retail-compatible Select Menu.
 *
 * The menu never performs these operations itself. Application services own
 * networking, local-server launch, persistence, overlays, and process exit.
 */
enum class MainMenuAction : std::uint8_t {
    tutorial,
    friends,
    leaderboard,
    settings,
    user_content,
    player_profile,
    create_match,
    join_match,
    quit,
    logout,
};

enum class MainMenuControlKind : std::uint8_t {
    square_button,
    text_button,
    navigation_item,
};

enum class WidgetVisualState : std::uint8_t {
    normal,
    hovered,
    pressed,
    focused,
    disabled,
};

enum class MainMenuAssetKind : std::uint8_t {
    texture,
    font,
    music,
    sound,
};

enum class TextureSampling : std::uint8_t {
    not_applicable,
    linear,
    nearest,
};

enum class TextureAnchor : std::uint8_t {
    not_applicable,
    top_left,
    center,
};

/** Retail loading metadata retained until it becomes a generic asset recipe. */
struct MainMenuAsset final {
    std::string_view path;
    MainMenuAssetKind kind{MainMenuAssetKind::texture};
    TextureSampling sampling{TextureSampling::linear};
    TextureAnchor anchor{TextureAnchor::top_left};
    double retail_scale{1.0};
};

/** Static presentation and routing data for one Select Menu control. */
struct MainMenuControl final {
    ui::Widget widget{};
    MainMenuAction action{MainMenuAction::join_match};
    MainMenuControlKind kind{MainMenuControlKind::text_button};
    std::string_view localization_key;
    std::string_view icon_asset;
};

/**
 * Renderer-independent reconstruction of the retail Select Menu.
 *
 * Retail coordinates are converted once from a bottom-left 800x600 canvas to
 * the native top-left canvas. One logical unit is one eighth of a retail pixel,
 * preserving the fractional square-button bounds without floating hit tests.
 */
class MainMenuModel final {
public:
    static constexpr std::int32_t reference_width_pixels{800};
    static constexpr std::int32_t reference_height_pixels{600};
    static constexpr std::int32_t subpixels_per_pixel{8};
    static constexpr std::size_t control_count{10U};

    MainMenuModel();

    [[nodiscard]] std::span<const MainMenuControl> controls() const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hovered() const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> focused() const noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;

    /**
     * Begins the retail pointer gesture.
     *
     * Retail buttons become armed on any left press delivered to the screen;
     * releasing over an enabled button activates that button. Keeping this
     * quirk here makes it explicit and testable instead of leaking into a
     * platform adapter.
     */
    void pointer_press(std::optional<ui::Point> point) noexcept;

    [[nodiscard]] std::optional<MainMenuAction>
    pointer_release(std::optional<ui::Point> point) noexcept;

    [[nodiscard]] std::optional<MainMenuAction> handle(ui::InputEvent event) noexcept;
    [[nodiscard]] bool set_enabled(MainMenuAction action, bool enabled) noexcept;
    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> index_of(ui::WidgetId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(MainMenuAction action) const noexcept;
    [[nodiscard]] std::optional<ui::WidgetId> hit_test(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<MainMenuAction> focused_action() const noexcept;
    [[nodiscard]] bool rebuild_focus() noexcept;

    std::array<MainMenuControl, control_count> controls_;
    ui::FocusNavigator focus_;
    std::optional<ui::WidgetId> hovered_;
    bool pointer_down_{false};
    bool navigation_item_armed_{false};
};

namespace main_menu_assets {

inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view frame{"png/ui/main_menu/frame_main_menu.png"};
inline constexpr std::string_view player_name_frame{"png/ui/main_menu/frame_player_name.png"};
inline constexpr std::string_view splash{"png/ui/splash.png"};
inline constexpr std::string_view button_left{
    "png/ui/common_elements/buttons/button_large_left.png"};
inline constexpr std::string_view button_middle{
    "png/ui/common_elements/buttons/button_large_mid.png"};
inline constexpr std::string_view button_right{
    "png/ui/common_elements/buttons/button_large_right.png"};
inline constexpr std::string_view square_button{
    "png/ui/common_elements/buttons/mm_button_square_default.png"};
inline constexpr std::string_view confirmation_sound{"sounds/menu_confirmA.ogg"};
inline constexpr std::string_view back_sound{"sounds/menu_backA.ogg"};
inline constexpr std::string_view music{"music/mainmenu.ogg"};
inline constexpr std::string_view button_font{"fonts/Spades.ttf"};
inline constexpr std::string_view welcome_font{"fonts/A750-Sans-Medium.ttf"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace main_menu_assets

} // namespace battlespades::frontend
