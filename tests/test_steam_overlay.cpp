#include "battlespades/platform/steam_overlay.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::platform::SteamOverlayInputAction;
using battlespades::platform::SteamOverlayInputGate;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void closed_overlay_changes_nothing() {
    SteamOverlayInputGate gate;
    expect(gate.observe(false, 0U) == SteamOverlayInputAction::none,
           "a closed overlay must leave input alone");
    expect(!gate.suspended(), "input starts live");
    expect(gate.reset() == SteamOverlayInputAction::none,
           "resetting a live gate has nothing to restore");
}

void opening_suspends_once_and_closing_resumes() {
    SteamOverlayInputGate gate;
    expect(gate.observe(true, 1U) == SteamOverlayInputAction::suspend,
           "Shift+Tab must free the mouse on the frame it is seen");
    expect(gate.suspended(), "input stays suspended while the overlay is up");
    expect(gate.observe(true, 1U) == SteamOverlayInputAction::none,
           "an overlay that stays open must not suspend every frame");
    expect(gate.observe(false, 1U) == SteamOverlayInputAction::resume,
           "closing the overlay must hand input back");
    expect(!gate.suspended(), "input is live again");
    expect(gate.observe(false, 1U) == SteamOverlayInputAction::none,
           "a resume is reported once");
}

void a_blink_between_frames_still_flushes_keys() {
    SteamOverlayInputGate gate;
    static_cast<void>(gate.observe(false, 0U));
    // Opened and closed inside one frame: the state reads closed again, but
    // the game saw Shift and Tab go down and never saw them come up.
    expect(gate.observe(false, 1U) == SteamOverlayInputAction::flush,
           "an activation missed between frames must still drop held keys");
    expect(!gate.suspended(), "a flush does not leave input suspended");
    expect(gate.observe(false, 1U) == SteamOverlayInputAction::none,
           "the same activation is flushed once");
}

void reopening_while_open_is_one_suspension() {
    SteamOverlayInputGate gate;
    expect(gate.observe(true, 1U) == SteamOverlayInputAction::suspend, "first open suspends");
    // Closed and reopened between frames (for example the invite dialog
    // replacing the overlay): still one suspension, no resume in between.
    expect(gate.observe(true, 2U) == SteamOverlayInputAction::none,
           "an overlay that is still up must not resume or re-suspend");
    expect(gate.observe(false, 2U) == SteamOverlayInputAction::resume, "then closing resumes");
}

void a_stopped_runtime_releases_input() {
    SteamOverlayInputGate gate;
    static_cast<void>(gate.observe(true, 4U));
    expect(gate.reset() == SteamOverlayInputAction::resume,
           "no closing callback arrives after Steam stops, so reset must resume");
    expect(!gate.suspended(), "reset leaves input live");
    // A restarted runtime counts from zero again; that is not an activation.
    expect(gate.observe(false, 0U) == SteamOverlayInputAction::none,
           "a fresh runtime's count is a baseline, not a missed activation");
    expect(gate.observe(false, 1U) == SteamOverlayInputAction::flush,
           "activations on the new runtime are seen again");
}

void a_count_that_goes_backwards_is_a_new_baseline() {
    SteamOverlayInputGate gate;
    static_cast<void>(gate.observe(false, 9U));
    expect(gate.observe(false, 2U) == SteamOverlayInputAction::none,
           "a smaller count means a new runtime, not an activation");
    expect(gate.observe(false, 3U) == SteamOverlayInputAction::flush,
           "counting resumes from the new baseline");
}

} // namespace

int main() {
    try {
        closed_overlay_changes_nothing();
        opening_suspends_once_and_closing_resumes();
        a_blink_between_frames_still_flushes_keys();
        reopening_while_open_is_one_suspension();
        a_stopped_runtime_releases_input();
        a_count_that_goes_backwards_is_a_new_baseline();
        std::cout << "steam overlay tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "steam overlay tests failed: " << error.what() << '\n';
        return 1;
    }
}
