#include "battlespades/frontend/classic_scoreboard_ping.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace battlespades;
using frontend::ClassicScoreboardPing;
using namespace std::chrono_literals;

namespace {
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}
void player(network::Protocol168Roster& roster, std::uint8_t id, std::string name) {
    network::CreatePlayerPacket packet;
    packet.player_id = id; packet.name = std::move(name); packet.team = 1;
    check(roster.apply(packet), "test player must enter roster");
}
network::ChatMessagePacket reply(std::string text, std::uint8_t sender = 255) {
    return {sender, 2, std::move(text)};
}
const auto start = ClassicScoreboardPing::Time{} + 100s;

void normal_lookup_and_refresh() {
    network::Protocol168Roster roster;
    player(roster, 0, "Local"); player(roster, 1, "Orion"); player(roster, 2, "Two Words");
    ClassicScoreboardPing pings;
    check(!pings.next_query(roster, 0, false, start), "closed scoreboard sends nothing");
    check(pings.next_query(roster, 0, true, start) == "/ping #1", "query uses ID and skips local");
    check(!pings.next_query(roster, 0, true, start+1s), "one query may be in flight");
    check(!pings.observe(reply("Orion's ping is 45 ms", 2), roster, start+1s), "player spoof remains visible");
    check(!pings.ping(*roster.player(1), start+1s), "player spoof cannot set ping");
    check(!pings.observe(reply("Babel apocalypse incoming"), roster, start+1s), "server notices stay visible");
    check(!pings.observe(reply("Two Words's ping is 99 ms"), roster, start+1s), "unrequested reply stays visible");
    check(!pings.ping(*roster.player(2), start+1s), "unrequested reply is not cached");
    check(pings.observe(reply("Orion's ping is 45 ms"), roster, start+1s), "automatic reply is consumed");
    check(pings.ping(*roster.player(1), start+1s) == 45, "server value reaches scoreboard cache");
    check(!pings.next_query(roster, 0, false, start+2s), "closing TAB stops further queries");
    check(pings.next_query(roster, 0, true, start+2s) == "/ping #2", "round robin reaches next player");
    check(pings.observe(reply("Two Words's ping is 0 ms."), roster, start+2100ms), "zero RTT and spaced names accepted");
    check(pings.ping(*roster.player(2), start+3s) == 0, "zero is distinct from unknown");
    check(!pings.next_query(roster, 0, true, start+4s), "fresh values are cached");
    check(!pings.next_query(roster, 0, true, start+30s), "opening TAB again does not flood commands");
    player(roster, 1, "Orion");
    check(pings.ping(*roster.player(1), start+31s) == 45, "ordinary respawn retains player ping");
    check(pings.next_query(roster, 0, true, start+62s) == "/ping #1", "older sample refreshes");
    check(!pings.ping(*roster.player(1), start+302s), "stale sample becomes unknown");
    pings.reset();
    check(!pings.ping(*roster.player(2), start+303s), "map/disconnect reset clears cache");
}

void command_failures_and_timeouts() {
    network::Protocol168Roster roster;
    player(roster, 0, "Local"); player(roster, 1, "Orion");
    for (const auto error : {"Unknown command", "You can't use this command",
                            "Please wait before executing your next command."}) {
        ClassicScoreboardPing pings;
        check(pings.next_query(roster, 0, true, start).has_value(), "probe is sent once");
        check(!pings.observe(reply(error), roster, start+1s), "server error is never swallowed");
        check(!pings.next_query(roster, 0, true, start+600s), "unsupported or rate-limited server stops polling");
    }
    ClassicScoreboardPing pings;
    check(pings.next_query(roster, 0, true, start).has_value(), "timeout probe");
    check(!pings.next_query(roster, 0, false, start+8s), "timeout advances even after closing TAB");
    check(!pings.next_query(roster, 0, true, start+60s), "unanswered probe disables automatic queries");
    check(!pings.observe(reply("Orion's ping is 80 ms"), roster, start+61s), "late response stays visible");
    check(!pings.ping(*roster.player(1), start+61s), "late response is not misattributed");
    pings.reset();
    check(pings.next_query(roster, 0, true, start+70s).has_value(), "new session can probe again");
    pings.send_failed();
    check(!pings.next_query(roster, 0, true, start+80s), "failed send cannot loop every frame");
}

void manual_queries_and_roster_reuse() {
    network::Protocol168Roster roster;
    player(roster, 0, "Local"); player(roster, 1, "Orion");
    ClassicScoreboardPing pings;
    pings.user_chat(start, true);
    check(!pings.next_query(roster, 0, true, start+1s), "manual commands get limiter headroom");
    check(!pings.observe(reply("Orion's ping is 78 ms"), roster, start+1s), "manual reply remains visible");
    check(pings.ping(*roster.player(1), start+1s) == 78, "manual reply also updates scoreboard");
    pings.reset();
    check(pings.next_query(roster, 0, true, start).has_value(), "concurrent automatic request");
    pings.user_chat(start+1s, true);
    check(!pings.observe(reply("Orion's ping is 90 ms"), roster, start+2s), "concurrent manual reply is not swallowed");
    check(pings.ping(*roster.player(1), start+2s) == 90, "concurrent query stores server RTT");

    pings.reset();
    check(pings.next_query(roster, 0, true, start).has_value(), "old slot request");
    pings.remove(1); roster.remove(1); player(roster, 1, "Orion");
    check(pings.observe(reply("Orion's ping is 90 ms"), roster, start+1s), "old automatic result is discarded");
    check(!pings.ping(*roster.player(1), start+1s), "reused ID and name cannot inherit old RTT");
    check(pings.next_query(roster, 0, true, start+2s).has_value(), "new occupant gets own request");
    check(pings.observe(reply("Orion's ping is 200 ms"), roster, start+3s), "new occupant result accepted");
    check(pings.ping(*roster.player(1), start+3s) == 200, "new RTT assigned");
    pings.remove(1); roster.remove(1); player(roster, 1, "Other");
    check(!pings.ping(*roster.player(1), start+4s), "leave clears cached sample");
}

void malformed_and_ambiguous_replies() {
    network::Protocol168Roster roster;
    player(roster, 0, "Local"); player(roster, 1, "Orion");
    ClassicScoreboardPing pings;
    check(pings.next_query(roster, 0, true, start).has_value(), "parser probe");
    for (const auto text : {"Orion's ping is -1 ms", "Orion's ping is 65536 ms",
             "Orion's ping is 10 ms and more", "Orion's ping is 1.5 ms", "Orion's ping is +2 ms",
             "Orion's ping is 9999999999999999999999 ms", "Not Orion's ping is 30 ms"}) {
        check(!pings.observe(reply(text), roster, start+1s), "malformed/unmatched reply is visible");
        check(!pings.ping(*roster.player(1), start+1s), "malformed/unmatched reply is not cached");
    }
    auto alert = reply("Orion's ping is 50 ms"); alert.chat_type = 3;
    check(!pings.observe(alert, roster, start+1s), "alert cannot be treated as ping response");
    check(pings.observe(reply("[*] Orion's ping is 65535 ms"), roster, start+1s), "bounded maximum and legacy prefix supported");
    pings.reset(); player(roster, 2, "Orion");
    check(!pings.next_query(roster, 0, true, start), "duplicate display names are not queried");
    pings.user_chat(start, true);
    check(!pings.observe(reply("Orion's ping is 5 ms"), roster, start+1s), "ambiguous manual result stays in chat");
    check(!pings.ping(*roster.player(1), start+1s) && !pings.ping(*roster.player(2), start+1s),
          "ambiguous result is never assigned to either player");
    roster.remove(2); player(roster, 1, "O'Brien's ping is Bob"); pings.reset();
    check(pings.next_query(roster, 0, true, start) == "/ping #1", "names are not interpolated into commands");
    check(pings.observe(reply("O'Brien's ping is Bob's ping is 15 ms"), roster, start+1s),
          "rightmost delimiter permits apostrophes and ping text in player names");
}
} // namespace

int main() {
    try {
        normal_lookup_and_refresh();
        command_failures_and_timeouts();
        manual_queries_and_roster_reuse();
        malformed_and_ambiguous_replies();
        std::cout << "Classic scoreboard ping scheduling, parsing and lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
