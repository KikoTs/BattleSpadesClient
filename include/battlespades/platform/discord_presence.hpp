#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::platform {

struct DiscordPresenceConfig final {
    bool enabled{true};
    bool allow_join{true};
    // Public application ID, never a bot token/client secret. Empty uses the
    // AOS_DISCORD_APPLICATION_ID environment variable or build configuration.
    std::string application_id;
    bool operator==(const DiscordPresenceConfig&) const = default;
};

struct DiscordPresenceActivity final {
    std::string server_name;
    std::string map;
    std::string mode;
    // Canonical numeric IPv4:port only. DNS, relay IDs and private addresses
    // are never advertised, even if they are valid game connection targets.
    std::string endpoint;
    int protocol{168}; // 168, 3 (0.75), or 4 (0.76).
    int players{};
    int maximum_players{};
    bool password_protected{};
    std::int64_t started_at{}; // Unix seconds; zero omits the elapsed timer.
};

enum class DiscordPresenceStatus { disabled, unconfigured, waiting_for_discord, connecting, ready };
enum class DiscordActivityAcknowledgement { none, published, cleared };

[[nodiscard]] bool valid_discord_application_id(std::string_view value) noexcept;
[[nodiscard]] std::string configured_discord_application_id();
[[nodiscard]] std::optional<std::string> discord_join_url(std::string_view endpoint, int protocol);
[[nodiscard]] std::optional<std::string> parse_discord_join_secret(std::string_view secret);

// All IPC lives on one worker. Main-thread calls only replace bounded state or
// drain a validated join request; no Discord installation is required.
class DiscordPresence final {
public:
    // Explicit local pipe/socket override is for isolated IPC integration tests.
    explicit DiscordPresence(std::string ipc_endpoint = {});
    ~DiscordPresence();
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    void configure(DiscordPresenceConfig config);
    void update(const DiscordPresenceActivity& activity);
    // Clears published presence promptly, bypassing the normal 15s coalescing.
    void clear();
    // Main thread must still use the normal connection/password/admission flow.
    [[nodiscard]] std::optional<std::string> poll_join_request();
    [[nodiscard]] DiscordPresenceStatus status() const noexcept;
    // The last SET_ACTIVITY payload Discord acknowledged on this connection.
    [[nodiscard]] DiscordActivityAcknowledgement activity_acknowledgement() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
