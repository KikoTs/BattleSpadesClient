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
using battlespades::frontend::IdentitySteamState;
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

void pointer_routes_login_register_steam_guest_and_recovery() {
    IdentityMenuModel model;
    model.pointer_press(Point{332 * 8, 420 * 8});
    expect(model.pointer_release(Point{332 * 8, 420 * 8}) ==
               IdentityAction::login,
           "left primary button should submit login");
    model.pointer_press(Point{468 * 8, 420 * 8});
    expect(model.pointer_release(Point{468 * 8, 420 * 8}) ==
               IdentityAction::register_account,
           "right primary button should submit registration");
    model.pointer_press(Point{400 * 8, 468 * 8});
    expect(!model.pointer_release(Point{400 * 8, 468 * 8}).has_value(),
           "Steam must remain hidden until the native runtime is ready");
    model.set_steam_available(true);
    model.pointer_press(Point{400 * 8, 468 * 8});
    expect(model.pointer_release(Point{400 * 8, 468 * 8}) ==
               IdentityAction::steam,
           "Steam button should select the native Steam identity when available");
    model.pointer_press(Point{332 * 8, 516 * 8});
    expect(model.pointer_release(Point{332 * 8, 516 * 8}) ==
               IdentityAction::guest,
           "wide secondary button should submit signed guest");
    model.show_recovery_form();
    expect(model.append_text("76561198000000001"), "recovery form accepts a SteamID rather than a display name");
    model.focus_next();
    expect(model.append_text("AOS-TEST-CODE") && model.phase() == IdentityMenuPhase::recovery_form,
           "recovery form accepts the backup credential in its masked field");

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

void steam_button_is_deterministic_across_attach_states() {
    IdentityMenuModel model;
    const Point steam{400 * 8, 468 * 8};
    model.set_steam_state(IdentitySteamState::connecting);
    expect(model.controls()[2].widget.state.visible &&
               model.controls()[2].label == "CONNECTING TO STEAM...",
           "a Steam runtime still attaching must show a pending button, not nothing");
    model.pointer_press(steam);
    expect(!model.pointer_release(steam).has_value(),
           "the pending Steam button must not submit");

    // reset_form (every bootstrap and sign-out) used to restore visibility
    // from a bool captured once at startup; the state must survive it.
    model.reset_form();
    expect(model.controls()[2].widget.state.visible,
           "reset_form must keep the pending Steam button visible");
    model.set_steam_state(IdentitySteamState::available);
    model.set_busy(true, "Signing in...");
    model.set_error("rejected");
    expect(model.controls()[2].widget.state.enabled &&
               model.controls()[2].label == "SIGN IN THROUGH STEAM",
           "an error after busy must re-enable the ready Steam button");
    model.pointer_press(steam);
    expect(model.pointer_release(steam) == IdentityAction::steam,
           "the ready Steam button must submit the Steam identity");

    model.set_steam_state(IdentitySteamState::connecting);
    model.set_error("again");
    expect(!model.controls()[2].widget.state.enabled,
           "set_error must not enable a Steam button that is still connecting");
    model.set_steam_state(IdentitySteamState::hidden);
    model.reset_form();
    expect(!model.controls()[2].widget.state.visible,
           "no Steam runtime at all hides the button");
}

} // namespace

void steam_link_choice_preserves_identity_and_requires_explicit_action() {
    IdentityMenuModel model;
    model.set_steam_available(true);
    model.show_steam_link("ExistingPlayer");
    expect(model.phase() == IdentityMenuPhase::steam_link, "linking has its own confirmation phase");
    expect(!model.append_text("ignored"), "link confirmation must not accept hidden credentials");
    model.pointer_press(Point{332 * 8, 420 * 8});
    expect(model.pointer_release(Point{332 * 8, 420 * 8}) == IdentityAction::link_steam,
           "linking is a distinct explicit action");
    model.pointer_press(Point{468 * 8, 420 * 8});
    expect(model.pointer_release(Point{468 * 8, 420 * 8}) == IdentityAction::keep_account,
           "players can retain their account without linking or switching");
    model.set_steam_state(IdentitySteamState::connecting);
    expect(!model.controls()[0].widget.state.enabled && model.controls()[1].widget.state.enabled,
           "Steam failure cannot disable keeping the existing account");
    model.reset_form();
    expect(model.controls()[0].action == IdentityAction::login &&
           model.controls()[1].action == IdentityAction::register_account,
           "returning to sign-in must not retain a link action");
}

int main() {
    try {
        form_never_exposes_clear_password_to_rendering();
        pointer_routes_login_register_steam_guest_and_recovery();
        field_focus_limits_and_busy_state_fail_closed();
        steam_button_is_deterministic_across_attach_states();
        presentation_uses_original_menu_assets();
        presentation_reserves_non_overlapping_text_bands();
        steam_link_choice_preserves_identity_and_requires_explicit_action();
        std::cout << "identity menu tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "identity menu tests failed: " << error.what() << '\n';
        return 1;
    }
}
