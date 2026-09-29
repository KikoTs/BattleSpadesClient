#pragma once

#include <cstdint>
#include <optional>

namespace battlespades::network {

/**
 * Retail GameScene clock sync and connection watchdog (stock gameScene.pyd).
 *
 * send_clock_sync (0x10181bc0): every CLOCK_SYNC_RATE loops, send
 * ClockSync(client_time = enet.time_get() % MAX_PING), unsequenced.
 * process_packet_clock_sync (0x10181f50):
 *   ping = time_get() % MAX_PING - client_time   (+MAX_PING when negative)
 *   latency = ping * 0.001 * 0.5
 *   estimate = server_loop_count + int(latency * UPDATE_FRAMERATE)
 *   loop_count = estimate only when |estimate - loop_count| > 10.
 * MAX_PING is PyInt_FromLong(10000) in initgameScene (0x101abb6b).
 */
inline constexpr std::int32_t clock_sync_max_ping_ms{10'000};
inline constexpr std::uint32_t clock_sync_rate_loops{60U};
inline constexpr std::int32_t max_clock_sync_difference{10};
inline constexpr std::int32_t update_framerate{60};

[[nodiscard]] constexpr std::int32_t clock_sync_client_time(std::uint64_t now_ms) noexcept {
    return static_cast<std::int32_t>(now_ms % static_cast<std::uint64_t>(clock_sync_max_ping_ms));
}

/** Round trip in milliseconds, wrapped at MAX_PING like retail. */
[[nodiscard]] constexpr std::int32_t clock_sync_ping_ms(std::uint64_t now_ms,
                                                        std::int32_t client_time) noexcept {
    auto ping = clock_sync_client_time(now_ms) - client_time;
    if (ping < 0) ping += clock_sync_max_ping_ms;
    return ping;
}

/**
 * The loop label retail adopts for this reply, or nothing inside the
 * +/-MAX_CLOCK_SYNC_DIFFERENCE dead band. The half-RTT lead moves the label
 * to where the server will be when the next ClientData arrives.
 */
[[nodiscard]] constexpr std::optional<std::int32_t>
clock_sync_relabel(std::int32_t loop_count, std::int32_t server_loop_count,
                   std::int32_t ping_ms) noexcept {
    const double latency = static_cast<double>(ping_ms) * 0.001 * 0.5;
    const auto estimate = static_cast<std::int64_t>(server_loop_count) +
                          static_cast<std::int64_t>(latency * update_framerate);
    const auto difference = estimate - static_cast<std::int64_t>(loop_count);
    if (difference <= max_clock_sync_difference && difference >= -max_clock_sync_difference) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(estimate);
}

/**
 * GameScene.update's liveness check: last = max(last_clock_sync,
 * last_world_update); after UPDATE_FRAMERATE * 5 silent loops show
 * CONNECTION_PROBLEMS ("Disconnect in {limit - elapsed:.1f}") every frame, and
 * disconnect with ERROR_TIMEOUT once elapsed exceeds the limit:
 * SERVER_TIMEOUT_BEFORE_FIRST_RESPONSE 20 s when nothing ever answered, else
 * SERVER_TIMEOUT_AFTER_FIRST_RESPONSE 30 s (60 s in UGC mode).
 */
class RetailConnectionWatchdog final {
public:
    static constexpr std::uint64_t warning_loops{static_cast<std::uint64_t>(update_framerate) * 5U};
    static constexpr double timeout_before_first_response{20.0};
    static constexpr double timeout_after_first_response{30.0};
    static constexpr double timeout_after_first_response_ugc{60.0};

    struct Verdict final {
        /** Seconds left before the disconnect while CONNECTION_PROBLEMS shows. */
        std::optional<double> seconds_left;
        bool timed_out{};
    };

    void reset() noexcept {
        loop_ = 0U;
        last_response_ = 0U;
    }
    /** A ClockSync reply or WorldUpdate arrived; it counts for the next loop. */
    void note_response() noexcept { last_response_ = loop_ + 1U; }
    [[nodiscard]] bool responded() const noexcept { return last_response_ != 0U; }
    /** Advance one fixed update and evaluate retail's check. */
    [[nodiscard]] Verdict tick(bool ugc_mode) noexcept {
        ++loop_;
        Verdict verdict;
        const auto silent = loop_ - last_response_;
        if (silent <= warning_loops) return verdict;
        const double limit = last_response_ == 0U ? timeout_before_first_response
                             : ugc_mode           ? timeout_after_first_response_ugc
                                                  : timeout_after_first_response;
        const double elapsed = static_cast<double>(silent) / update_framerate;
        if (elapsed > limit) {
            verdict.timed_out = true;
        } else {
            verdict.seconds_left = limit - elapsed;
        }
        return verdict;
    }

private:
    std::uint64_t loop_{};
    std::uint64_t last_response_{};
};

} // namespace battlespades::network
