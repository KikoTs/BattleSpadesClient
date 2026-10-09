#include "../src/platform/steam_auth_ticket_state.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using battlespades::platform::detail::SteamAuthTicketState;
using battlespades::platform::detail::SteamTicketKind;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

void callbacks_are_scoped_and_tickets_live_until_cancelled() {
    SteamAuthTicketState state;
    const std::array raw{std::byte{0xaa}, std::byte{0xbb}};
    expect(state.begin(1U, SteamTicketKind::web_api, 480U, 42U), "WebAPI may carry configured 480 identity");
    expect(state.begin(2U, SteamTicketKind::session, 224540U, 0x0102030405060708ULL, raw),
           "retail ticket starts independently");
    expect(!state.take(1U, SteamTicketKind::web_api), "never expose bytes before callback");
    state.complete(1U, SteamTicketKind::session, true);
    expect(!state.take(1U, SteamTicketKind::web_api), "wrong callback kind cannot finish ticket");
    state.complete(2U, SteamTicketKind::session, true);
    const auto session = state.take(2U, SteamTicketKind::session);
    expect(session && session->hex == "0807060504030201aabb" && session->error.empty(),
           "retail XOR key is little-endian SteamID plus unmodified ticket in lowercase hex");
    expect(!state.take(2U, SteamTicketKind::session), "completion consumed once");
    state.complete(1U, SteamTicketKind::web_api, true, raw);
    const auto web = state.take(1U, SteamTicketKind::web_api);
    expect(web && web->hex == "aabb" && web->app_id == 480U && web->steam_id == 42U,
           "WebAPI contains only raw ticket and actual app/subject");
    expect(state.cancel(1U) && state.entries().contains(2U), "WebAPI cancellation must leave the game ticket valid");
    expect(state.cancel(2U), "taken ticket remains tracked for later cancellation");
    state.complete(2U, SteamTicketKind::session, true);
    expect(!state.take(2U, SteamTicketKind::session), "late callbacks cannot resurrect cancellation");
}

void failure_and_bounds_never_publish_tickets() {
    SteamAuthTicketState state;
    const std::array raw{std::byte{0xaa}};
    expect(!state.begin(1U, SteamTicketKind::session, 480U, 42U, raw), "Spacewar never supplies retail auth");
    expect(!state.begin(0U, SteamTicketKind::web_api, 224540U, 42U), "invalid handle refused");
    expect(!state.begin(1U, SteamTicketKind::web_api, 224540U, 0U), "invalid subject refused");
    expect(state.begin(1U, SteamTicketKind::session, 224540U, 42U, raw), "start session");
    state.complete(1U, SteamTicketKind::session, false);
    const auto failure = state.take(1U, SteamTicketKind::session);
    expect(failure && !failure->error.empty() && failure->hex.empty(), "Steam failure wipes provisional bytes");
    expect(state.cancel(1U), "failed ticket still cancellable");
    expect(state.begin(2U, SteamTicketKind::web_api, 224540U, 42U), "start web request");
    std::array<std::byte, SteamAuthTicketState::maximum_web_api_bytes + 1U> oversized{};
    state.complete(2U, SteamTicketKind::web_api, true, oversized);
    const auto invalid = state.take(2U, SteamTicketKind::web_api);
    expect(invalid && invalid->hex.empty() && !invalid->error.empty(), "oversized callback rejected");
    expect(state.cancel(2U), "cancel malformed response");
    for (std::uint32_t handle{1U}; handle <= SteamAuthTicketState::maximum_live_tickets; ++handle) {
        expect(state.begin(handle, SteamTicketKind::web_api, 224540U, 42U), "bounded slots available");
    }
    expect(state.full() && !state.begin(9U, SteamTicketKind::web_api, 224540U, 42U), "issuance bounded");
    expect(state.cancel(1U) && state.begin(9U, SteamTicketKind::web_api, 224540U, 42U), "cancellation releases the slot");
}
}

int main() {
    try {
        callbacks_are_scoped_and_tickets_live_until_cancelled();
        failure_and_bounds_never_publish_tickets();
        std::cout << "Steam authentication ticket tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
