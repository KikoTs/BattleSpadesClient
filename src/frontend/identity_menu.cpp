#include "battlespades/frontend/identity_menu.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::int32_t scale{IdentityMenuModel::subpixels_per_pixel};

[[nodiscard]] constexpr ui::Rect design_rect(std::int32_t x,
                                             std::int32_t y,
                                             std::int32_t width,
                                             std::int32_t height) noexcept {
    return ui::Rect{x * scale, y * scale, width * scale, height * scale};
}

[[nodiscard]] constexpr ui::Widget widget(std::uint32_t id,
                                          ui::Rect bounds) noexcept {
    return ui::Widget{ui::WidgetId{id}, bounds, {}};
}

[[nodiscard]] bool contains(ui::Rect bounds, ui::Point point) noexcept {
    return point.x > bounds.x && point.y > bounds.y &&
           point.x < bounds.x + bounds.width &&
           point.y < bounds.y + bounds.height;
}

[[nodiscard]] std::size_t previous_utf8_boundary(std::string_view value) noexcept {
    auto boundary = value.size();
    if (boundary == 0U) return 0U;
    --boundary;
    while (boundary > 0U &&
           (static_cast<unsigned char>(value[boundary]) & 0xC0U) == 0x80U) {
        --boundary;
    }
    return boundary;
}

} // namespace

IdentityMenuModel::IdentityMenuModel()
    : controls_{
          IdentityControl{
              widget(1U, design_rect(269, 406, 126, 46)),
              IdentityAction::login,
              "SIGN IN",
          },
          IdentityControl{
              widget(2U, design_rect(405, 406, 126, 46)),
              IdentityAction::register_account,
              "REGISTER",
          },
          IdentityControl{
              widget(3U, design_rect(269, 462, 262, 46)),
              IdentityAction::guest,
              "PLAY AS GUEST",
          },
          IdentityControl{
              widget(4U, design_rect(319, 450, 162, 46)),
              IdentityAction::acknowledge_recovery,
              "CONTINUE",
          },
      } {
    controls_[3].widget.state.visible = false;
}

std::span<const IdentityControl> IdentityMenuModel::controls() const noexcept {
    return controls_;
}

IdentityMenuPhase IdentityMenuModel::phase() const noexcept {
    return phase_;
}

IdentityField IdentityMenuModel::focused_field() const noexcept {
    return focused_field_;
}

std::string_view IdentityMenuModel::username() const noexcept {
    return username_;
}

std::string_view IdentityMenuModel::password() const noexcept {
    return password_;
}

std::string IdentityMenuModel::masked_password() const {
    // Do not mirror UTF-8 byte length: one glyph per code point avoids leaking
    // the clear password into the renderer's texture-cache key.
    std::string result;
    result.reserve(password_.size());
    for (std::size_t index = 0U; index < password_.size();) {
        result.push_back('*');
        ++index;
        while (index < password_.size() &&
               (static_cast<unsigned char>(password_[index]) & 0xC0U) ==
                   0x80U) {
            ++index;
        }
    }
    return result;
}

std::string_view IdentityMenuModel::status() const noexcept {
    return status_;
}

std::string_view IdentityMenuModel::error() const noexcept {
    return error_;
}

std::string_view IdentityMenuModel::recovery_code() const noexcept {
    return recovery_code_;
}

bool IdentityMenuModel::busy() const noexcept {
    return busy_;
}

WidgetVisualState IdentityMenuModel::visual_state(ui::WidgetId id) const noexcept {
    const auto found = std::ranges::find(controls_, id, [](const IdentityControl& control) {
        return control.widget.id;
    });
    if (found == controls_.end() || !found->widget.state.visible ||
        !found->widget.state.enabled || busy_) {
        return WidgetVisualState::disabled;
    }
    const auto index =
        static_cast<std::size_t>(std::distance(controls_.begin(), found));
    if (pressed_ == index && hovered_ == index) return WidgetVisualState::pressed;
    if (hovered_ == index) return WidgetVisualState::hovered;
    return WidgetVisualState::normal;
}

ui::Rect IdentityMenuModel::username_bounds() const noexcept {
    return design_rect(269, 280, 262, 42);
}

ui::Rect IdentityMenuModel::password_bounds() const noexcept {
    return design_rect(269, 348, 262, 42);
}

void IdentityMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_test(*point) : std::nullopt;
}

void IdentityMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    pressed_ = hovered_;
    if (!point.has_value() || phase_ != IdentityMenuPhase::form || busy_) return;
    if (field_hit(username_bounds(), *point)) {
        focused_field_ = IdentityField::username;
    } else if (field_hit(password_bounds(), *point)) {
        focused_field_ = IdentityField::password;
    }
}

std::optional<IdentityAction>
IdentityMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    std::optional<IdentityAction> action;
    if (!busy_ && pressed_.has_value() && hovered_ == pressed_) {
        const auto& control = controls_[*pressed_];
        if (control.widget.state.visible && control.widget.state.enabled) {
            action = control.action;
        }
    }
    pressed_.reset();
    return action;
}

void IdentityMenuModel::focus(IdentityField field) noexcept {
    if (phase_ == IdentityMenuPhase::form && !busy_) focused_field_ = field;
}

void IdentityMenuModel::focus_next() noexcept {
    if (phase_ != IdentityMenuPhase::form || busy_) return;
    focused_field_ = focused_field_ == IdentityField::username
                         ? IdentityField::password
                         : IdentityField::username;
}

bool IdentityMenuModel::append_text(std::string_view utf8) {
    if (phase_ != IdentityMenuPhase::form || busy_ || utf8.empty() ||
        utf8.find('\0') != std::string_view::npos) {
        return false;
    }
    auto& target =
        focused_field_ == IdentityField::username ? username_ : password_;
    const auto maximum = focused_field_ == IdentityField::username
                             ? maximum_username_bytes
                             : maximum_password_bytes;
    if (target.size() + utf8.size() > maximum) return false;
    if (focused_field_ == IdentityField::username &&
        !std::ranges::all_of(utf8, [](char character) {
            const auto value = static_cast<unsigned char>(character);
            return std::isalnum(value) != 0 || character == '_';
        })) {
        return false;
    }
    target.append(utf8);
    error_.clear();
    return true;
}

bool IdentityMenuModel::erase_code_point() noexcept {
    if (phase_ != IdentityMenuPhase::form || busy_) return false;
    auto& target =
        focused_field_ == IdentityField::username ? username_ : password_;
    if (target.empty()) return false;
    target.resize(previous_utf8_boundary(target));
    error_.clear();
    return true;
}

void IdentityMenuModel::clear_password() noexcept {
    std::fill(password_.begin(), password_.end(), '\0');
    password_.clear();
}

void IdentityMenuModel::set_busy(bool busy, std::string status) {
    busy_ = busy;
    status_ = std::move(status);
    error_.clear();
    for (auto& control : controls_) control.widget.state.enabled = !busy;
}

void IdentityMenuModel::set_error(std::string error) {
    busy_ = false;
    status_.clear();
    error_ = std::move(error);
    for (auto& control : controls_) control.widget.state.enabled = true;
}

void IdentityMenuModel::show_recovery_code(std::string code) {
    clear_password();
    busy_ = false;
    status_.clear();
    error_.clear();
    recovery_code_ = std::move(code);
    phase_ = IdentityMenuPhase::recovery_code;
    for (std::size_t index = 0U; index < controls_.size(); ++index) {
        controls_[index].widget.state.visible = index == 3U;
        controls_[index].widget.state.enabled = true;
    }
    hovered_.reset();
    pressed_.reset();
}

void IdentityMenuModel::reset_form() noexcept {
    clear_password();
    status_.clear();
    error_.clear();
    recovery_code_.clear();
    phase_ = IdentityMenuPhase::form;
    focused_field_ = IdentityField::username;
    for (std::size_t index = 0U; index < controls_.size(); ++index) {
        controls_[index].widget.state.visible = index != 3U;
        controls_[index].widget.state.enabled = true;
    }
    busy_ = false;
    hovered_.reset();
    pressed_.reset();
}

std::optional<std::size_t> IdentityMenuModel::hit_test(ui::Point point) const noexcept {
    for (std::size_t index = controls_.size(); index-- > 0U;) {
        if (controls_[index].widget.state.visible &&
            field_hit(controls_[index].widget.bounds, point)) {
            return index;
        }
    }
    return std::nullopt;
}

bool IdentityMenuModel::field_hit(ui::Rect bounds, ui::Point point) const noexcept {
    return contains(bounds, point);
}

} // namespace battlespades::frontend
