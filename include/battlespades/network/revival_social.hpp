#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace battlespades::network {

enum class RevivalSocialRequestKind : std::uint8_t {
    sync,
    presence_offline,
    find_friends,
    friend_action,
    list_lobbies,
    create_lobby,
    lobby_action,
};

/** One immutable HTTPS operation. Player actions use the priority lane. */
struct RevivalSocialRequest final {
    std::uint64_t generation{};
    RevivalSocialRequestKind kind{RevivalSocialRequestKind::sync};
    bool priority{};
    std::string coalesce_key;
    std::string cursor{"0"};
    std::string client_instance_id;
    std::string presence{"online"};
    std::string query;
    std::string lobby_id;
    std::string action;
    std::string target;
    nlohmann::json payload{nlohmann::json::object()};
};

struct RevivalSocialFriend final {
    std::string legacy_id;
    std::string public_id;
    std::string nickname;
    std::string username;
    std::string presence{"offline"};
    std::string friendship_status;
    std::string direction;
    std::string current_lobby_id;

    [[nodiscard]] friend bool operator==(const RevivalSocialFriend&,
                                         const RevivalSocialFriend&) = default;
};

struct RevivalSocialInvitation final {
    std::string id;
    std::string lobby_id;
    std::string lobby_name;
    std::string inviter_id;
    std::string inviter_name;

    [[nodiscard]] friend bool operator==(const RevivalSocialInvitation&,
                                         const RevivalSocialInvitation&) = default;
};

struct RevivalSocialLobbyMember final {
    std::string legacy_id;
    std::string nickname;
    std::string presence{"online"};
    bool in_game{};
    nlohmann::json member_data{nlohmann::json::object()};

    [[nodiscard]] friend bool operator==(const RevivalSocialLobbyMember&,
                                         const RevivalSocialLobbyMember&) = default;
};

struct RevivalSocialLobby final {
    std::string id;
    std::string owner_id;
    std::string name;
    std::string privacy{"invite"};
    std::string state{"idle"};
    std::string revision;
    std::string server_id;
    std::size_t maximum_members{24U};
    nlohmann::json settings{nlohmann::json::object()};
    std::vector<RevivalSocialLobbyMember> members;

    [[nodiscard]] friend bool operator==(const RevivalSocialLobby&,
                                         const RevivalSocialLobby&) = default;
};

struct RevivalSocialEvent final {
    std::string id;
    std::string type;
    std::string lobby_id;
    std::string actor_id;
    nlohmann::json payload{nlohmann::json::object()};

    [[nodiscard]] friend bool operator==(const RevivalSocialEvent&,
                                         const RevivalSocialEvent&) = default;
};

struct RevivalSocialSnapshot final {
    std::string cursor{"0"};
    std::vector<RevivalSocialFriend> friends;
    std::vector<RevivalSocialInvitation> invitations;
    std::vector<RevivalSocialLobby> lobbies;
    std::optional<RevivalSocialLobby> lobby;
    std::vector<RevivalSocialEvent> events;
};

struct RevivalSocialResult final {
    RevivalSocialRequest request;
    RevivalSocialSnapshot snapshot;
    std::optional<RevivalSocialFriend> found_player;
    std::string error_code;
    std::string error;
    long http_status{};

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

struct RevivalSocialBounds final {
    std::size_t maximum_friends{512U};
    std::size_t maximum_invitations{128U};
    std::size_t maximum_lobbies{128U};
    std::size_t maximum_members{64U};
    std::size_t maximum_events{256U};
};

/** Parse one AoSPlay response, discarding malformed/duplicate rows deterministically. */
[[nodiscard]] RevivalSocialResult parse_revival_social_response(
    RevivalSocialRequest request,
    std::string_view json,
    RevivalSocialBounds bounds = {});

struct RevivalSocialClientConfig final {
    std::size_t maximum_normal_requests{64U};
    std::size_t maximum_priority_requests{32U};
    std::size_t maximum_results{128U};
    std::chrono::milliseconds menu_poll_interval{3'000};
    std::chrono::milliseconds active_lobby_poll_interval{1'000};
    std::chrono::milliseconds maximum_backoff{30'000};
};

struct RevivalSocialClientStatus final {
    bool available{};
    bool closing{};
    bool normal_active{};
    bool priority_active{};
    std::size_t normal_queued{};
    std::size_t priority_queued{};
    std::chrono::milliseconds retry_after{};
    std::string last_error;
};

/**
 * Two-lane, bounded social worker.
 *
 * Periodic reads are serialized in the normal lane. Start/Join/Leave and
 * friend mutations use a separate single-action lane, so a slow poll cannot
 * freeze a button. Results are delivered only by drain() on the UI thread.
 */
class RevivalSocialClient final {
public:
    using Executor = std::function<RevivalSocialResult(
        const RevivalSocialRequest&, std::stop_token)>;

    explicit RevivalSocialClient(Executor executor,
                                 RevivalSocialClientConfig config = {});
    ~RevivalSocialClient();

    RevivalSocialClient(const RevivalSocialClient&) = delete;
    RevivalSocialClient& operator=(const RevivalSocialClient&) = delete;
    RevivalSocialClient(RevivalSocialClient&&) = delete;
    RevivalSocialClient& operator=(RevivalSocialClient&&) = delete;

    [[nodiscard]] bool enqueue(RevivalSocialRequest request);
    void set_available(bool available) noexcept;
    void set_presence(std::string presence, nlohmann::json metadata = {});
    void tick(std::chrono::steady_clock::time_point now);
    [[nodiscard]] std::vector<RevivalSocialResult> drain(
        std::chrono::steady_clock::time_point now);
    [[nodiscard]] RevivalSocialSnapshot snapshot() const;
    [[nodiscard]] RevivalSocialClientStatus status(
        std::chrono::steady_clock::time_point now) const;

    /** Best-effort presence clear followed by cancellable, bounded shutdown. */
    void shutdown(std::chrono::milliseconds grace = std::chrono::milliseconds{250}) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::network
