#pragma once

#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_runtime.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <string>

namespace battlespades::frontend {

/** Classic has no per-player RTT packet. Query the server's /ping command
 * only while the scoreboard is open, one request at a time, at most once per
 * two seconds. All timestamps use a monotonic clock. Unsupported servers stop
 * after one unanswered query; no guessed values enter the roster. */
class ClassicScoreboardPing final {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    void reset() noexcept;
    void remove(std::uint8_t player_id) noexcept;
    void user_chat(Time now, bool ping_command = false) noexcept;
    void send_failed() noexcept;
    [[nodiscard]] std::optional<std::string> next_query(
        const network::Protocol168Roster& roster, std::uint8_t local_id,
        bool scoreboard_open, Time now);
    /** Returns true only for an exact reply to our own silent request. Manual
     * replies and unrelated server/player messages remain in the chat feed. */
    [[nodiscard]] bool observe(const network::ChatMessagePacket& message,
        const network::Protocol168Roster& roster, Time now);
    [[nodiscard]] std::optional<std::uint16_t> ping(
        const network::RemotePlayerReplica& player, Time now) const noexcept;

private:
    struct Sample {
        std::string name;
        Time received{};
        std::optional<std::uint16_t> value;
    };
    struct Pending {
        std::uint8_t id{};
        std::string name;
        Time deadline{};
        bool silent{true}, removed{};
    };
    std::array<Sample, 128> samples_{};
    std::optional<Pending> pending_;
    Time next_query_at_{};
    Time manual_reply_until_{};
    std::size_t cursor_{};
    bool disabled_{};
};

} // namespace battlespades::frontend
