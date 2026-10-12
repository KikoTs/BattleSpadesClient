#include "battlespades/frontend/classic_scoreboard_ping.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace battlespades::frontend {
namespace {
using namespace std::chrono_literals;
constexpr auto query_interval = 2s;
constexpr auto reply_timeout = 8s;
constexpr auto refresh_interval = 60s;
constexpr auto sample_lifetime = 300s;

struct PingReply {
    std::string_view name;
    std::uint16_t value{};
};

// piqueserver/core_commands/info.py and historical pyspades use this reply.
// Parse from the right so names may themselves contain apostrophes or "ping".
std::optional<PingReply> parse_reply(std::string_view text) {
    if (text.starts_with("[*] ")) text.remove_prefix(4);
    constexpr std::string_view marker = "'s ping is ";
    const auto separator = text.rfind(marker);
    if (separator == std::string_view::npos || separator == 0) return std::nullopt;
    const auto name = text.substr(0, separator);
    auto number = text.substr(separator + marker.size());
    if (number.ends_with(" ms.")) number.remove_suffix(4);
    else if (number.ends_with(" ms")) number.remove_suffix(3);
    else return std::nullopt;
    if (number.empty() || number.size() > 5 ||
        number.find_first_not_of("0123456789") != std::string_view::npos) return std::nullopt;
    unsigned value{};
    const auto result = std::from_chars(number.data(), number.data()+number.size(), value);
    if (result.ec != std::errc{} || result.ptr != number.data()+number.size() || value > 65535)
        return std::nullopt;
    return PingReply{name, static_cast<std::uint16_t>(value)};
}
} // namespace

void ClassicScoreboardPing::reset() noexcept { *this = {}; }

void ClassicScoreboardPing::remove(std::uint8_t id) noexcept {
    if (id < samples_.size()) samples_[id] = {};
    // Keep the request until its reply/deadline so a reused slot (even with the
    // same name) cannot inherit a departed player's in-flight result.
    if (pending_ && pending_->id == id) pending_->removed = true;
}

void ClassicScoreboardPing::user_chat(Time now, bool ping_command) noexcept {
    // A concurrent manual /ping is indistinguishable on the wire. Keep all
    // ensuing replies visible and leave room in the server command limiter.
    if (pending_) pending_->silent = false;
    next_query_at_ = std::max(next_query_at_, now + 5s);
    if (ping_command) manual_reply_until_ = now + reply_timeout;
}

void ClassicScoreboardPing::send_failed() noexcept {
    pending_.reset();
    disabled_ = true;
}

std::optional<std::string> ClassicScoreboardPing::next_query(
    const network::Protocol168Roster& roster, std::uint8_t local_id,
    bool scoreboard_open, Time now) {
    if (pending_ && now >= pending_->deadline) {
        if (!pending_->removed) disabled_ = true;
        pending_.reset();
    }
    if (!scoreboard_open || disabled_ || pending_ || now < next_query_at_) return std::nullopt;
    for (std::size_t offset = 0; offset < samples_.size(); ++offset) {
        const auto slot = (cursor_ + offset) % samples_.size();
        const auto id = static_cast<std::uint8_t>(slot);
        const auto* player = roster.player(id);
        if (!player || id == local_id || player->name.empty()) continue;
        const auto& sample = samples_[slot];
        if (sample.value && sample.name == player->name && now - sample.received < refresh_interval)
            continue;
        // Replies contain names, not IDs. Ambiguous names cannot be attributed.
        bool duplicate{};
        for (const auto& other : roster.present_players()) {
            if (other.player_id != id && other.name == player->name) { duplicate = true; break; }
        }
        if (duplicate) continue;
        cursor_ = (slot + 1) % samples_.size();
        next_query_at_ = now + query_interval;
        pending_ = Pending{id, player->name, now + reply_timeout};
        // Numeric selection avoids quoting, partial-name matches and commands
        // embedded in names, and works with piqueserver's get_player().
        return "/ping #" + std::to_string(id);
    }
    next_query_at_ = now + query_interval;
    return std::nullopt;
}

bool ClassicScoreboardPing::observe(const network::ChatMessagePacket& message,
    const network::Protocol168Roster& roster, Time now) {
    // The Classic adapter normalizes authenticated server-origin chat to 255.
    // A player writing a lookalike reply must never alter the scoreboard.
    if (message.player_id != 255 || message.chat_type > 2) return false;
    if (pending_ && now >= pending_->deadline) {
        if (!pending_->removed) disabled_ = true;
        pending_.reset();
        return false; // do not cache or hide a late, uncorrelated reply
    }
    const auto reply = parse_reply(message.value);
    if (!reply) {
        // Do not hide command failures or other server notices. Back off for
        // the session; the error remains available to the player.
        if (pending_ && (message.value == "Unknown command" ||
                        message.value == "You can't use this command" ||
                        message.value == "Please wait before executing your next command.")) {
            send_failed();
        }
        return false;
    }
    const bool matching = pending_ && pending_->name == reply->name;
    if (!matching && now >= manual_reply_until_) return false;
    const bool silent = matching && pending_->silent;
    if (matching && pending_->removed) {
        pending_.reset();
        return silent;
    }
    const network::RemotePlayerReplica* target{};
    for (const auto& player : roster.present_players()) {
        if (player.name != reply->name) continue;
        if (target) return false; // never choose between duplicate names
        target = &player;
    }
    if (!target || (matching && target->player_id != pending_->id)) return false;
    samples_[target->player_id] = Sample{target->name, now, reply->value};
    if (matching) pending_.reset();
    return silent;
}

std::optional<std::uint16_t> ClassicScoreboardPing::ping(
    const network::RemotePlayerReplica& player, Time now) const noexcept {
    if (player.player_id >= samples_.size()) return std::nullopt;
    const auto& sample = samples_[player.player_id];
    if (sample.name != player.name || now < sample.received ||
        now - sample.received >= sample_lifetime) return std::nullopt;
    return sample.value;
}
} // namespace battlespades::frontend
