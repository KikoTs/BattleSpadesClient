#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace battlespades::frontend {

enum class IdentityField : std::uint8_t {
    username,
    password,
};

enum class IdentityAction : std::uint8_t {
    login,
    register_account,
    steam,
    guest,
    acknowledge_recovery,
    recover_steam,
    link_steam,
    keep_account,
};

/**
 * What the Steam sign-in button shows.
 *
 * `connecting` keeps the button in place, disabled, while the retail Steam
 * runtime is still attaching; it used to be hidden until then, so whether the
 * player saw it depended on how fast Steam answered on that launch.
 */
enum class IdentitySteamState : std::uint8_t {
    hidden,
    connecting,
    available,
};

enum class IdentityMenuPhase : std::uint8_t {
    form,
    recovery_code,
    recovery_form,
    steam_link,
};

struct IdentityControl final {
    ui::Widget widget{};
    IdentityAction action{IdentityAction::login};
    std::string_view label;
};

/**
 * Renderer-independent account gate shown before the Select Menu.
 *
 * The model owns only short-lived form text and pointer/focus state. It never
 * persists credentials, performs HTTP, or exposes the clear password to draw
 * commands. Authentication work is performed by RevivalIdentityService on a
 * worker and reported through set_busy/set_error/show_recovery_code.
 */
class IdentityMenuModel final {
public:
    static constexpr std::int32_t subpixels_per_pixel{
        MainMenuModel::subpixels_per_pixel};
    static constexpr std::size_t maximum_username_bytes{24U};
    static constexpr std::size_t maximum_password_bytes{256U};
    static constexpr std::size_t control_count{6U};

    IdentityMenuModel();

    [[nodiscard]] std::span<const IdentityControl> controls() const noexcept;
    [[nodiscard]] IdentityMenuPhase phase() const noexcept;
    [[nodiscard]] IdentityField focused_field() const noexcept;
    [[nodiscard]] std::string_view username() const noexcept;
    [[nodiscard]] std::string_view password() const noexcept;
    [[nodiscard]] std::string masked_password() const;
    [[nodiscard]] std::string_view status() const noexcept;
    [[nodiscard]] std::string_view error() const noexcept;
    [[nodiscard]] std::string_view recovery_code() const noexcept;
    [[nodiscard]] std::string_view link_account_name() const noexcept { return link_account_name_; }
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool steam_available() const noexcept;
    [[nodiscard]] IdentitySteamState steam_state() const noexcept;
    [[nodiscard]] WidgetVisualState visual_state(ui::WidgetId id) const noexcept;
    [[nodiscard]] ui::Rect username_bounds() const noexcept;
    [[nodiscard]] ui::Rect password_bounds() const noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<IdentityAction>
    pointer_release(std::optional<ui::Point> point) noexcept;

    void focus(IdentityField field) noexcept;
    void focus_next() noexcept;
    [[nodiscard]] bool append_text(std::string_view utf8);
    [[nodiscard]] bool erase_code_point() noexcept;
    void clear_password() noexcept;
    void set_busy(bool busy, std::string status = {});
    /** Show Steam only after the owned native retail runtime is ready. */
    void set_steam_available(bool available) noexcept;
    /** Hidden, connecting (visible, disabled) or available. */
    void set_steam_state(IdentitySteamState state) noexcept;
    void set_error(std::string error);
    void show_recovery_code(std::string code, std::string saved_location = {});
    void show_recovery_form();
    void show_steam_link(std::string account_name);
    void reset_form() noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> hit_test(ui::Point point) const noexcept;
    [[nodiscard]] bool field_hit(ui::Rect bounds, ui::Point point) const noexcept;
    void apply_steam_control() noexcept;

    std::array<IdentityControl, control_count> controls_;
    std::string username_;
    std::string password_;
    std::string status_;
    std::string error_;
    std::string recovery_code_;
    std::string link_account_name_;
    IdentityMenuPhase phase_{IdentityMenuPhase::form};
    IdentityField focused_field_{IdentityField::username};
    std::optional<std::size_t> hovered_;
    std::optional<std::size_t> pressed_;
    bool busy_{};
    IdentitySteamState steam_state_{IdentitySteamState::hidden};
};

} // namespace battlespades::frontend
