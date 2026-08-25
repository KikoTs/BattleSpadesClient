#include "battlespades/frontend/identity_menu.hpp"
#include "battlespades/frontend/identity_presentation.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using battlespades::frontend::IdentityAction;
using battlespades::frontend::IdentityField;
using battlespades::frontend::IdentityMenuModel;
using battlespades::frontend::IdentityMenuPhase;
using battlespades::frontend::IdentityPresentation;
using battlespades::frontend::IdentityPresentationContext;
using battlespades::ui::Point;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void form_never_exposes_clear_password_to_rendering() {
    IdentityMenuModel model;
    expect(model.append_text("KikoTs"), "username should accept protocol-safe text");
    model.focus(IdentityField::password);
    expect(model.append_text("secret-password"), "password should accept printable UTF-8");
    expect(model.masked_password() == "***************",
           "password renderer must receive one mask per code point");

    const auto list = IdentityPresentation{}.build(
        model, IdentityPresentationContext{{800, 600}, 1'000U});
    for (const auto& command : list.commands()) {
        if (const auto* text = std::get_if<TextDrawCommand>(&command)) {
            expect(text->localization_key.find("secret-password") ==
                       std::string::npos,
                   "draw list must never cache the clear password");
        }
    }
}

void pointer_routes_login_register_guest_and_recovery() {
    IdentityMenuModel model;
    model.pointer_press(Point{332 * 8, 429 * 8});
    expect(model.pointer_release(Point{332 * 8, 429 * 8}) ==
               IdentityAction::login,
           "left primary button should submit login");
    model.pointer_press(Point{468 * 8, 429 * 8});
    expect(model.pointer_release(Point{468 * 8, 429 * 8}) ==
               IdentityAction::register_account,
           "right primary button should submit registration");
    model.pointer_press(Point{400 * 8, 485 * 8});
    expect(model.pointer_release(Point{400 * 8, 485 * 8}) ==
               IdentityAction::guest,
           "wide secondary button should submit signed guest");

    model.show_recovery_code("AOS-AAAAAA-BBBBBB-CCCCCC-DDDDDD");
    expect(model.phase() == IdentityMenuPhase::recovery_code,
           "registration must stop at the recovery acknowledgement");
    model.pointer_press(Point{400 * 8, 473 * 8});
    expect(model.pointer_release(Point{400 * 8, 473 * 8}) ==
               IdentityAction::acknowledge_recovery,
           "recovery code must require an explicit Continue");
}

void field_focus_limits_and_busy_state_fail_closed() {
    IdentityMenuModel model;
    model.pointer_press(Point{300 * 8, 250 * 8});
    expect(model.focused_field() == IdentityField::username,
           "username field should take pointer focus");
    expect(!model.append_text("bad name"),
           "username must reject spaces before hitting the API");
    expect(model.append_text("Valid_Name"), "username should accept account alphabet");
    model.focus_next();
    expect(model.focused_field() == IdentityField::password,
           "Tab should toggle the two credential fields");
    expect(model.append_text("long password value"),
           "password should accept spaces");
    model.set_busy(true, "Signing in...");
    model.pointer_press(Point{332 * 8, 429 * 8});
    expect(!model.pointer_release(Point{332 * 8, 429 * 8}).has_value(),
           "busy authentication must disable duplicate submissions");
}

void presentation_reserves_non_overlapping_text_bands() {
    const IdentityMenuModel model;
    const auto list = IdentityPresentation{}.build(
        model, IdentityPresentationContext{{800, 600}, 1'000U});
    const TextDrawCommand* title{};
    const TextDrawCommand* username{};
    for (const auto& command : list.commands()) {
        const auto* candidate = std::get_if<TextDrawCommand>(&command);
        if (candidate == nullptr) continue;
        if (candidate->localization_key == "PLAYER IDENTITY") title = candidate;
        if (candidate->localization_key == "USERNAME") username = candidate;
    }
    expect(title != nullptr && username != nullptr,
           "identity title and username label must both be drawn");
    expect(title->destination.y + title->destination.height <=
               username->destination.y,
           "identity title must not overlap the first field label");
    expect(model.username_bounds().y / 8 >= 280 &&
               model.password_bounds().y / 8 >= 348,
           "credential fields must remain below the logo/title band");
}

void presentation_uses_original_menu_assets() {
    const IdentityMenuModel model;
    const auto list = IdentityPresentation{}.build(
        model, IdentityPresentationContext{{1280, 720}, 1'000U});
    expect(!list.empty(), "identity gate must produce a draw list");
    const auto* background =
        std::get_if<SpriteDrawCommand>(&list.commands().front());
    expect(background != nullptr &&
               background->asset_id == "png/ui/ugc_splash.png",
           "identity gate should preserve the main-menu backdrop");
}

} // namespace

int main() {
    try {
        form_never_exposes_clear_password_to_rendering();
        pointer_routes_login_register_guest_and_recovery();
        field_focus_limits_and_busy_state_fail_closed();
        presentation_uses_original_menu_assets();
        presentation_reserves_non_overlapping_text_bands();
        std::cout << "identity menu tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "identity menu tests failed: " << error.what() << '\n';
        return 1;
    }
}
