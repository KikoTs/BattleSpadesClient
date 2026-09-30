#include "battlespades/frontend/loading_screen.hpp"
#include "battlespades/frontend/server_password_prompt.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_session.hpp"
#include "battlespades/network/server_discovery.hpp"
#include "battlespades/platform/steam_connect.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace battlespades;
using frontend::ServerPasswordPromptModel;
using network::Protocol168Session;
using network::Protocol168SessionConfig;
using network::Protocol168SessionPhase;

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

[[nodiscard]] std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (const auto value : values) result.push_back(static_cast<std::byte>(value));
    return result;
}

/** The server's outer framing: prefix 0x30, then the body as one LZF literal run. */
[[nodiscard]] std::vector<std::byte> server_datagram(std::span<const std::byte> packet) {
    expect(!packet.empty() && packet.size() <= 32U, "one literal run holds 1..32 bytes");
    std::vector<std::byte> result{std::byte{0x30},
                                  static_cast<std::byte>(packet.size() - 1U)};
    result.insert(result.end(), packet.begin(), packet.end());
    return result;
}

[[nodiscard]] std::string text_of(std::span<const std::byte> datagram, std::size_t skip) {
    std::string result;
    for (std::size_t index{skip}; index < datagram.size(); ++index) {
        result.push_back(static_cast<char>(datagram[index]));
    }
    return result;
}

[[nodiscard]] Protocol168Session connected_session(std::string password = {}) {
    Protocol168SessionConfig config;
    config.auto_join = false;
    config.server_password = std::move(password);
    Protocol168Session session{std::move(config)};
    expect(!session.connected().empty(), "the ticket packet opens the handshake");
    return session;
}

void the_packet_is_id_text_and_a_terminator() {
    const auto packet = network::encode_password_provided_packet("s3cret");
    expect(packet == bytes({113U, 's', '3', 'c', 'r', 'e', 't', 0U}),
           "PasswordProvided is 113, the UTF-8 text, a NUL");
    expect(network::encode_password_provided_packet("").empty(),
           "an empty password is not sent");
    expect(network::encode_password_provided_packet(std::string(64U, 'a')).size() == 66U &&
               network::encode_password_provided_packet(std::string(65U, 'a')).empty(),
           "64 bytes is the longest password the server compares");
    expect(network::encode_password_provided_packet(std::string_view{"a\0b", 3U}).empty(),
           "a NUL cannot be part of a NUL-terminated password");
}

void a_request_opens_the_prompt_and_an_answer_closes_it() {
    auto session = connected_session();
    expect(session.password_prompt() == network::Protocol168PasswordPrompt{},
           "an open server never shows a prompt");
    expect(session.provide_password("early").empty(), "nothing to answer before a request");

    const auto needed = bytes({112U});
    auto result = session.ingest(server_datagram(needed));
    expect(result.accepted && result.outbound_datagrams.empty(),
           "PasswordNeeded is accepted and waits for the player");
    auto prompt = session.password_prompt();
    expect(prompt.pending && !prompt.rejected && prompt.requests == 1U,
           "the first request is a plain prompt");
    expect(session.phase() == Protocol168SessionPhase::awaiting_initial_info,
           "the handshake still waits for InitialInfo");

    expect(session.provide_password("").empty() &&
               session.provide_password(std::string(65U, 'x')).empty() &&
               session.password_prompt().pending,
           "text that cannot be a password keeps the prompt open");

    const auto answer = session.provide_password("letmein");
    expect(!answer.empty() && text_of(answer, answer.size() - 9U) ==
                                  std::string{"\x71letmein\0", 9U},
           "the answer is one PasswordProvided datagram");
    expect(!session.password_prompt().pending, "the prompt closes once answered");
    expect(session.provide_password("again").empty(), "one answer per request");

    result = session.ingest(server_datagram(needed));
    prompt = session.password_prompt();
    expect(result.accepted && prompt.pending && prompt.rejected && prompt.requests == 2U,
           "a second request means the answer was wrong");
}

void a_known_password_answers_the_first_request_only() {
    auto session = connected_session("hunter2");
    const auto needed = bytes({112U});
    auto result = session.ingest(server_datagram(needed));
    expect(result.accepted && result.outbound_datagrams.size() == 1U,
           "a supplied password answers without a prompt");
    const auto& sent = result.outbound_datagrams.front();
    expect(text_of(sent, sent.size() - 9U) == std::string{"\x71hunter2\0", 9U},
           "with PasswordProvided");
    expect(!session.password_prompt().pending && !session.password_prompt().rejected,
           "and no prompt is shown");

    result = session.ingest(server_datagram(needed));
    expect(result.accepted && result.outbound_datagrams.empty() &&
               session.password_prompt().pending && session.password_prompt().rejected,
           "a refused supplied password is never sent twice: the prompt takes over");
}

void a_malformed_or_late_request_fails_the_join() {
    auto session = connected_session();
    const auto padded = bytes({112U, 0U});
    static_cast<void>(session.ingest(server_datagram(padded)));
    expect(session.phase() == Protocol168SessionPhase::failed,
           "PasswordNeeded is the id byte alone");
}

void a_kick_during_the_exchange_is_a_wrong_password() {
    network::LiveProtocol168Status status;
    status.phase = network::LiveProtocol168Phase::disconnected;
    status.disconnect_reason = 2U;
    expect(network::protocol168_password_failure(status) ==
               network::PasswordJoinFailure::none,
           "a kick without a password request is an ordinary kick");

    status.password.requests = 3U;
    expect(network::protocol168_password_failure(status) ==
               network::PasswordJoinFailure::wrong_password,
           "reason 2 during the exchange is the refusal");
    status.disconnect_reason = 11U;
    expect(network::protocol168_password_failure(status) ==
               network::PasswordJoinFailure::timed_out,
           "reason 11 during the exchange is the unanswered prompt");
    status.disconnect_reason = 18U;
    expect(network::protocol168_password_failure(status) ==
               network::PasswordJoinFailure::none,
           "a map change at the prompt is a reconnect, not a refusal");

    status.disconnect_reason = 2U;
    status.initial_info = std::make_shared<const network::Protocol168InitialInfo>();
    expect(network::protocol168_password_failure(status) ==
               network::PasswordJoinFailure::none,
           "after InitialInfo the password was accepted: a kick is a kick");
}

void the_box_masks_and_limits_what_is_typed() {
    ServerPasswordPromptModel prompt;
    expect(!prompt.visible() && !prompt.append_text("x") && !prompt.submit().has_value(),
           "a hidden box takes no input");

    prompt.open(false);
    expect(prompt.visible() && !prompt.rejected() && prompt.empty() &&
               prompt.masked().empty(),
           "PasswordNeeded shows an empty box");
    expect(!prompt.submit().has_value() && prompt.visible(),
           "Enter on an empty box sends nothing");

    expect(prompt.append_text("pa") && prompt.append_text("ß"),
           "committed text is appended");
    expect(prompt.masked() == "***", "one star per character, not per byte");
    expect(!prompt.append_text("\n") && !prompt.append_text(std::string_view{"a\0", 2U}),
           "control characters are refused");
    expect(!prompt.append_text(std::string(64U, 'x')) && prompt.masked() == "***",
           "text that would pass 64 bytes is refused whole");

    expect(prompt.erase_character() && prompt.masked() == "**",
           "backspace removes a whole UTF-8 character");
    const auto answer = prompt.submit();
    expect(answer.has_value() && *answer == "pa" && !prompt.visible() && prompt.empty(),
           "Enter hands the text over, hides the box and forgets the text");

    prompt.open(true);
    expect(prompt.visible() && prompt.rejected() && prompt.empty(),
           "a refused answer reopens an empty box with the note");
    prompt.close();
    expect(!prompt.visible() && !prompt.rejected(), "closing clears the note");
    expect(ServerPasswordPromptModel::box_bounds == ui::Rect{70, 400, 350, 20},
           "EditBoxControl(70, 180, 350, 20) in top-left pixels");
}

void links_carry_the_password() {
    auto plain = frontend::split_endpoint_password("play.example.net:27015");
    expect(plain.endpoint == "play.example.net:27015" && plain.password.empty(),
           "an address without a password is unchanged");

    auto link = frontend::split_endpoint_password("aos://1.2.3.4:27015?password=open%20sesame");
    expect(link.endpoint == "aos://1.2.3.4:27015" && link.password == "open sesame",
           "?password= is split off and percent-decoded");

    network::ServerEndpoint endpoint;
    std::string error;
    expect(network::parse_server_endpoint(link.endpoint, endpoint, error) &&
               endpoint.port == 27015U,
           "what is left is an ordinary endpoint");

    auto bare = frontend::split_endpoint_password("1.2.3.4?password=x");
    expect(bare.endpoint == "1.2.3.4" && bare.password == "x" &&
               network::parse_server_endpoint(bare.endpoint, endpoint, error) &&
               endpoint.port == network::default_game_port,
           "a link without a port still means 27015");

    for (const auto* broken : {"h:1?password=", "h:1?password=%zz", "h:1?password=%0a",
                               "h:1?password=%4"}) {
        const auto result = frontend::split_endpoint_password(broken);
        expect(result.password.empty() && result.endpoint == broken &&
                   !network::parse_server_endpoint(result.endpoint, endpoint, error),
               "a broken password suffix stays on the address and the address is refused");
    }
    const auto long_password =
        frontend::split_endpoint_password("h:1?password=" + std::string(65U, 'a'));
    expect(long_password.password.empty(), "a password over 64 bytes is not taken");

    const auto steam = platform::parse_steam_join_target("+connect 1.2.3.4:27015 +password pw1");
    expect(steam.has_value() && steam->endpoint == "1.2.3.4:27015" && steam->password == "pw1",
           "a Steam launch line may carry +password");
    const auto hosted = platform::parse_steam_join_target("+password pw2 +connect steam:5");
    expect(hosted.has_value() && hosted->steam_id == 5U && hosted->password == "pw2",
           "in either order");
    expect(!platform::parse_steam_join_target("+connect 1.2.3.4:1 +password").has_value() &&
               !platform::parse_steam_join_target(
                    "+connect 1.2.3.4:1 +password a +password b").has_value() &&
               !platform::parse_steam_join_target(
                    "+connect 1.2.3.4:1 +password " + std::string(65U, 'a')).has_value(),
           "a missing, repeated or oversized password refuses the whole value");
    const auto open = platform::parse_steam_join_target("+connect 1.2.3.4:27015");
    expect(open.has_value() && open->password.empty(), "no password is the usual case");
}

void the_listing_marks_password_servers() {
    const auto listing = network::parse_public_server_list(R"([
        {"name": "Open", "ip": "10.0.0.1", "port": 27015, "map": "London", "mode_tla": "tdm",
         "tags": ["mode=0006"]},
        {"name": "Locked", "ip": "10.0.0.2", "port": 27015, "map": "London", "mode_tla": "tdm",
         "tags": ["mode=0006", "password"]},
        {"name": "Flagged", "ip": "10.0.0.3", "port": 27015, "map": "London",
         "mode_tla": "tdm", "password": true}
    ])");
    expect(static_cast<bool>(listing) && listing.servers.size() == 3U,
           "the three rows parse");
    const auto find = [&listing](std::string_view name) {
        return *std::ranges::find(listing.servers, name, &network::DiscoveredServer::name);
    };
    expect(!find("Open").password_protected, "an open server has no padlock");
    expect(find("Locked").password_protected, "the heartbeat tag `password` marks the row");
    expect(find("Flagged").password_protected, "as does a boolean field");
}

void the_loader_waits_while_the_player_types() {
    frontend::MatchLoadingModel loading;
    loading.begin("London", "tdm");
    loading.set_waiting_for_player(true);
    for (int second{}; second < 120; ++second) loading.tick(1.0);
    expect(loading.snapshot().state != frontend::MatchLoadingState::timed_out,
           "the no-progress timeout waits with the password prompt");

    loading.set_waiting_for_player(false);
    expect(loading.snapshot().no_progress_seconds_remaining == 30.0,
           "answering restarts the 30 seconds");
    for (int second{}; second < 31; ++second) loading.tick(1.0);
    expect(loading.snapshot().state == frontend::MatchLoadingState::timed_out,
           "and a silent server still times out");
}

} // namespace

int main() {
    try {
        the_packet_is_id_text_and_a_terminator();
        a_request_opens_the_prompt_and_an_answer_closes_it();
        a_known_password_answers_the_first_request_only();
        a_malformed_or_late_request_fails_the_join();
        a_kick_during_the_exchange_is_a_wrong_password();
        the_box_masks_and_limits_what_is_typed();
        links_carry_the_password();
        the_listing_marks_password_servers();
        the_loader_waits_while_the_player_types();
    } catch (const std::exception& error) {
        std::cerr << "server password test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
