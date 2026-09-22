#include "battlespades/network/revival_social.hpp"

#include "battlespades/core/diagnostics.hpp"
#include "battlespades/core/utf8.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace battlespades::network {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string text(const Json& object,
                               std::string_view key,
                               std::size_t maximum = 96U) {
    const auto found = object.find(key);
    if (found == object.end()) return {};
    std::string value;
    if (found->is_string()) value = found->get<std::string>();
    else if (found->is_number_integer()) value = std::to_string(found->get<std::int64_t>());
    else if (found->is_number_unsigned()) value = std::to_string(found->get<std::uint64_t>());
    return core::utf8_code_point_prefix(value, maximum);
}

[[nodiscard]] bool boolean(const Json& object,
                           std::string_view key,
                           bool fallback = false) noexcept {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

[[nodiscard]] std::size_t bounded_size(const Json& object,
                                       std::string_view key,
                                       std::size_t fallback,
                                       std::size_t maximum) noexcept {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) return fallback;
    const auto value = found->get<std::int64_t>();
    return value > 0 ? (std::min)(static_cast<std::size_t>(value), maximum) : fallback;
}

[[nodiscard]] std::optional<RevivalSocialFriend> friend_record(const Json& value) {
    if (!value.is_object()) return std::nullopt;
    RevivalSocialFriend result;
    result.legacy_id = text(value, "legacy_id", 64U);
    result.public_id = text(value, "public_id", 64U);
    result.nickname = text(value, "nickname", 32U);
    result.username = text(value, "username", 32U);
    result.presence = text(value, "presence", 24U);
    result.friendship_status = text(value, "friendship_status", 24U);
    result.direction = text(value, "direction", 24U);
    result.current_lobby_id = text(value, "current_lobby_id", 64U);
    result.current_server_id = text(value, "current_server_id", 128U);
    if (result.legacy_id.empty() || (result.nickname.empty() && result.username.empty())) {
        return std::nullopt;
    }
    if (result.presence.empty()) result.presence = "offline";
    return result;
}

[[nodiscard]] std::optional<RevivalSocialInvitation> invitation_record(const Json& value) {
    if (!value.is_object()) return std::nullopt;
    RevivalSocialInvitation result;
    result.id = text(value, "id", 64U);
    result.lobby_id = text(value, "lobby_id", 64U);
    result.lobby_name = text(value, "lobby_name", 64U);
    if (const auto inviter = value.find("inviter");
        inviter != value.end() && inviter->is_object()) {
        result.inviter_id = text(*inviter, "legacy_id", 64U);
        result.inviter_name = text(*inviter, "nickname", 32U);
    }
    if (result.id.empty()) result.id = result.lobby_id;
    if (result.id.empty() || result.lobby_id.empty()) return std::nullopt;
    if (result.lobby_name.empty()) result.lobby_name = "Lobby";
    if (result.inviter_name.empty()) result.inviter_name = "Unknown";
    return result;
}

[[nodiscard]] std::optional<RevivalSocialLobbyMember> member_record(const Json& value) {
    if (!value.is_object()) return std::nullopt;
    RevivalSocialLobbyMember result;
    result.legacy_id = text(value, "legacy_id", 64U);
    result.nickname = text(value, "nickname", 32U);
    result.presence = text(value, "presence", 24U);
    result.in_game = boolean(value, "in_game");
    if (const auto data = value.find("member_data"); data != value.end() && data->is_object()) {
        result.member_data = *data;
        if (const auto status = data->find("in-game"); status != data->end()) {
            result.in_game = result.in_game || *status == "1" || *status == true;
        }
    }
    if (result.legacy_id.empty()) return std::nullopt;
    if (result.nickname.empty()) result.nickname = result.legacy_id;
    if (result.presence.empty()) result.presence = "online";
    return result;
}

[[nodiscard]] std::optional<RevivalSocialLobby> lobby_record(
    const Json& value,
    std::size_t maximum_members) {
    if (!value.is_object()) return std::nullopt;
    RevivalSocialLobby result;
    result.id = text(value, "id", 64U);
    result.owner_id = text(value, "owner_id", 64U);
    result.name = text(value, "name", 64U);
    result.privacy = text(value, "privacy", 24U);
    result.lobby_type = text(value, "lobby_type", 16U);
    if (result.lobby_type.empty()) result.lobby_type = "normal";
    result.member_count = bounded_size(value, "member_count", 0U, maximum_members);
    result.state = text(value, "state", 24U);
    result.revision = text(value, "revision", 64U);
    result.server_id = text(value, "server_id", 128U);
    result.start_id = text(value, "start_id", 64U);
    result.maximum_members = bounded_size(value, "max_members", 24U, maximum_members);
    if (const auto settings = value.find("settings");
        settings != value.end() && settings->is_object()) {
        result.settings = *settings;
    }
    if (const auto members = value.find("members");
        members != value.end() && members->is_array()) {
        std::set<std::string, std::less<>> seen;
        for (const auto& row : *members) {
            if (result.members.size() >= maximum_members) break;
            auto member = member_record(row);
            if (member.has_value() && seen.insert(member->legacy_id).second) {
                result.members.push_back(std::move(*member));
            }
        }
    }
    if (result.id.empty() || result.state == "closed") return std::nullopt;
    if (result.name.empty()) result.name = "Revival Lobby";
    if (result.privacy.empty()) result.privacy = "invite";
    if (result.state.empty()) result.state = "idle";
    return result;
}

void parse_friends(const Json& body,
                   std::string_view key,
                   std::size_t maximum,
                   std::vector<RevivalSocialFriend>& output) {
    const auto rows = body.find(key);
    if (rows == body.end() || !rows->is_array()) return;
    std::set<std::string, std::less<>> seen;
    for (const auto& value : *rows) {
        if (output.size() >= maximum) break;
        auto row = friend_record(value);
        if (row.has_value() && seen.insert(row->legacy_id).second) {
            output.push_back(std::move(*row));
        }
    }
}

[[nodiscard]] std::chrono::milliseconds doubled(
    std::chrono::milliseconds value,
    std::chrono::milliseconds ceiling) noexcept {
    if (value >= ceiling / 2) return ceiling;
    return (std::min)(value * 2, ceiling);
}

/**
 * Give every running client its own presence lease.
 *
 * AoSPlay identifies a presence publisher by this UUID. Reusing one value
 * across two game processes lets either process delete the other's presence
 * during shutdown, which is especially visible while hosting a lobby and
 * testing with a second client. The API validates this field as a canonical
 * UUID, so prefixes or undelimited random hexadecimal strings are rejected.
 */
[[nodiscard]] std::string make_client_instance_id() {
    static std::atomic_uint64_t nonce{};
    std::array<std::uint32_t, 4U> words{};
    std::random_device source;
    const auto clock = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto serial = nonce.fetch_add(1U, std::memory_order_relaxed);
    for (std::size_t index{}; index < words.size(); ++index) {
        const auto clock_word = static_cast<std::uint32_t>(
            clock >> ((index % 2U) * 32U));
        const auto serial_word = static_cast<std::uint32_t>(
            serial >> ((index % 2U) * 32U));
        words[index] = source() ^ clock_word ^ serial_word ^
                       static_cast<std::uint32_t>(index * 0x9E3779B9U);
    }

    // RFC 4122/9562 version 4 and variant bits. Keep the canonical 8-4-4-4-12
    // spelling because the AoSPlay request schema parses this as a UUID.
    const auto time_high_and_version =
        static_cast<std::uint32_t>(0x4000U | (words[1U] & 0x0FFFU));
    const auto clock_sequence = static_cast<std::uint32_t>(
        0x8000U | ((words[2U] >> 16U) & 0x3FFFU));
    std::ostringstream result;
    result << std::hex << std::nouppercase << std::setfill('0')
           << std::setw(8) << words[0U] << '-'
           << std::setw(4) << ((words[1U] >> 16U) & 0xFFFFU) << '-'
           << std::setw(4) << time_high_and_version << '-'
           << std::setw(4) << clock_sequence << '-'
           << std::setw(4) << (words[2U] & 0xFFFFU)
           << std::setw(8) << words[3U];
    return result.str();
}

} // namespace

RevivalSocialResult parse_revival_social_response(RevivalSocialRequest request,
                                                   std::string_view json,
                                                   RevivalSocialBounds bounds) {
    RevivalSocialResult result;
    result.request = std::move(request);
    Json body;
    try {
        body = Json::parse(json);
    } catch (...) {
        result.error_code = "invalid_response";
        result.error = "AoSPlay returned malformed social data.";
        return result;
    }
    if (!body.is_object()) {
        result.error_code = "invalid_response";
        result.error = "AoSPlay returned a non-object social response.";
        return result;
    }

    result.snapshot.cursor = text(body, "cursor", 64U);
    if (result.snapshot.cursor.empty()) result.snapshot.cursor = result.request.cursor;
    result.has_friends = body.contains("friends") && body["friends"].is_array();
    result.has_lobby = body.contains("lobby");
    result.has_invitations = body.contains("invitations") && body["invitations"].is_array();
    if ((body.contains("friends") && !result.has_friends) ||
        (body.contains("invitations") && !result.has_invitations)) {
        result.error_code = "invalid_response";
        result.error = "AoSPlay returned malformed social collections.";
        return result;
    }
    parse_friends(body, "friends", bounds.maximum_friends, result.snapshot.friends);
    if (const auto players = body.find("players");
        players != body.end() && players->is_array()) {
        std::set<std::string, std::less<>> seen;
        for (const auto& value : *players) {
            if (result.found_players.size() >= bounds.maximum_friends) break;
            auto row = friend_record(value);
            if (row.has_value() && seen.insert(row->legacy_id).second) {
                result.found_players.push_back(std::move(*row));
            }
        }
    }
    if (const auto player = body.find("player"); player != body.end()) {
        result.found_player = friend_record(*player);
    }
    if (!result.found_player.has_value() && !result.found_players.empty()) {
        result.found_player = result.found_players.front();
    } else if (result.found_player.has_value() && result.found_players.empty()) {
        result.found_players.push_back(*result.found_player);
    }

    if (const auto invitations = body.find("invitations");
        invitations != body.end() && invitations->is_array()) {
        std::set<std::string, std::less<>> seen;
        for (const auto& value : *invitations) {
            if (result.snapshot.invitations.size() >= bounds.maximum_invitations) break;
            auto row = invitation_record(value);
            if (row.has_value() && seen.insert(row->id).second) {
                result.snapshot.invitations.push_back(std::move(*row));
            }
        }
    }

    if (const auto current = body.find("lobby"); current != body.end() && !current->is_null()) {
        result.snapshot.lobby = lobby_record(*current, bounds.maximum_members);
        if (!result.snapshot.lobby) {
            result.error_code = "invalid_response";
            result.error = "AoSPlay returned malformed lobby membership.";
            return result;
        }
    }
    if (const auto lobbies = body.find("lobbies"); lobbies != body.end() && lobbies->is_array()) {
        std::set<std::string, std::less<>> seen;
        for (const auto& value : *lobbies) {
            if (result.snapshot.lobbies.size() >= bounds.maximum_lobbies) break;
            auto row = lobby_record(value, bounds.maximum_members);
            if (row.has_value() && seen.insert(row->id).second) {
                result.snapshot.lobbies.push_back(std::move(*row));
            }
        }
    }

    if (const auto events = body.find("events"); events != body.end() && events->is_array()) {
        std::set<std::string, std::less<>> seen;
        for (const auto& value : *events) {
            if (result.snapshot.events.size() >= bounds.maximum_events) break;
            if (!value.is_object()) continue;
            RevivalSocialEvent event;
            event.id = text(value, "id", 64U);
            event.type = text(value, "type", 64U);
            event.lobby_id = text(value, "lobby_id", 64U);
            event.actor_id = text(value, "actor_id", 64U);
            if (const auto payload = value.find("payload");
                payload != value.end() && payload->is_object()) {
                event.payload = *payload;
            }
            if (!event.id.empty() && !event.type.empty() && seen.insert(event.id).second) {
                result.snapshot.events.push_back(std::move(event));
            }
        }
    }
    return result;
}

std::string new_revival_social_id() { return make_client_instance_id(); }

class RevivalSocialClient::Impl final {
public:
    struct Lane final {
        std::deque<RevivalSocialRequest> queue;
        bool active{};
        std::jthread worker;
    };

    Impl(Executor source_executor, RevivalSocialClientConfig source_config)
        : executor{std::move(source_executor)}, config{source_config},
          client_instance_id{make_client_instance_id()},
          backoff{config.menu_poll_interval} {
        if (!executor || config.maximum_normal_requests == 0U ||
            config.maximum_priority_requests == 0U || config.maximum_results == 0U ||
            config.menu_poll_interval <= std::chrono::milliseconds::zero() ||
            config.active_lobby_poll_interval <= std::chrono::milliseconds::zero() ||
            config.maximum_backoff < config.menu_poll_interval) {
            throw std::invalid_argument{"invalid Revival social client bounds"};
        }
        normal.worker = std::jthread{[this](std::stop_token stop) { run(normal, stop); }};
        priority.worker = std::jthread{[this](std::stop_token stop) { run(priority, stop); }};
    }

    ~Impl() { shutdown(std::chrono::milliseconds{0}); }

    [[nodiscard]] bool enqueue(RevivalSocialRequest request) {
        std::scoped_lock lock{mutex};
        if (closing || request.generation == 0U) return false;
        // Writes always share one ordered lane, including callers that omit
        // the priority hint. A friend action must not wait behind a slow poll.
        if (request.kind == RevivalSocialRequestKind::friend_action ||
            request.kind == RevivalSocialRequestKind::create_lobby ||
            request.kind == RevivalSocialRequestKind::lobby_action) request.priority = true;
        auto& lane = request.priority ? priority : normal;
        const auto maximum = request.priority ? config.maximum_priority_requests
                                              : config.maximum_normal_requests;
        if (!request.coalesce_key.empty()) {
            std::erase_if(lane.queue, [&](const RevivalSocialRequest& pending) {
                return pending.coalesce_key == request.coalesce_key;
            });
        }
        if (lane.queue.size() >= maximum) return false;
        request.submission_sequence = next_submission_sequence++;
        if (request.priority) newest_priority_submission = request.submission_sequence;
        lane.queue.push_back(std::move(request));
        wake.notify_all();
        return true;
    }

    void set_available(bool value) noexcept {
        std::scoped_lock lock{mutex};
        enabled = value;
        connected = value;
        if (value) {
            next_poll = std::chrono::steady_clock::time_point{};
            backoff = config.menu_poll_interval;
            last_error.clear();
        } else {
            std::erase_if(normal.queue, [](const RevivalSocialRequest& request) {
                return request.kind == RevivalSocialRequestKind::sync;
            });
            last_error = "Social features require an online account.";
        }
    }

    void set_presence(std::string value, Json value_metadata) {
        std::scoped_lock lock{mutex};
        presence = core::utf8_code_point_prefix(value, 24U);
        if (presence.empty()) presence = "online";
        metadata = value_metadata.is_object() ? std::move(value_metadata) : Json::object();
        next_poll = std::chrono::steady_clock::time_point{};
    }

    void tick(std::chrono::steady_clock::time_point now) {
        std::scoped_lock lock{mutex};
        // A priority mutation and a sync must never begin together.  A sync
        // already running before the mutation is rejected in drain() using
        // submission_sequence; holding new polls here closes the opposite
        // ordering where a later-enqueued poll reaches AoSPlay first.
        if (!enabled || closing || now < next_poll || poll_pending_locked() ||
            priority.active || !priority.queue.empty()) {
            return;
        }
        RevivalSocialRequest request;
        request.generation = next_generation++;
        request.submission_sequence = next_submission_sequence++;
        request.kind = RevivalSocialRequestKind::sync;
        request.coalesce_key = "social-sync";
        request.cursor = snapshot.cursor;
        request.client_instance_id = client_instance_id;
        request.presence = presence;
        request.payload = metadata;
        if (normal.queue.size() < config.maximum_normal_requests) {
            normal.queue.push_back(std::move(request));
            wake.notify_all();
        }
    }

    [[nodiscard]] std::vector<RevivalSocialResult> drain(
        std::chrono::steady_clock::time_point now) {
        std::vector<RevivalSocialResult> output;
        std::scoped_lock lock{mutex};
        output.reserve(results.size());
        while (!results.empty()) {
            auto result = std::move(results.front());
            results.pop_front();
            if (result) filter_new_events_locked(result.snapshot);
            if (result.request.kind == RevivalSocialRequestKind::sync &&
                result.request.submission_sequence != 0U &&
                result.request.submission_sequence < newest_priority_submission) {
                auto events = std::move(result.snapshot.events);
                result.snapshot = snapshot;
                result.snapshot.events = std::move(events);
                next_poll = now;
                output.push_back(std::move(result));
                continue;
            }
            if (result.request.kind == RevivalSocialRequestKind::sync) {
                if (result) {
                    if (older_lobby(result.snapshot.lobby) ||
                        (!result.has_lobby && !result.snapshot.lobby)) {
                        result.snapshot.lobby = snapshot.lobby;
                    }
                    if (!result.has_friends && result.snapshot.friends.empty())
                        result.snapshot.friends = snapshot.friends;
                    if (!result.has_invitations && result.snapshot.invitations.empty())
                        result.snapshot.invitations = snapshot.invitations;
                    snapshot = result.snapshot;
                    connected = true;
                    last_error.clear();
                    backoff = snapshot.lobby.has_value() ? config.active_lobby_poll_interval
                                                         : config.menu_poll_interval;
                    // A discarded pre-mutation read is not evidence of the
                    // current state.  Poll again immediately after the
                    // mutation lane settles instead of showing stale data for
                    // another full menu interval.
                    next_poll = now + backoff;
                } else {
                    last_error = result.error;
                    connected = false;
                    if (result.error_code == "authentication_required") enabled = false;
                    next_poll = now + backoff;
                    backoff = doubled(backoff, config.maximum_backoff);
                }
            } else if (!result) {
                last_error = result.error;
                if (result.error_code == "authentication_required") {
                    enabled = false;
                    connected = false;
                } else if (result.http_status == 0L) {
                    connected = false;
                }
                // A timed-out write may have committed server-side.  Force an
                // authoritative read so the UI can converge on the outcome
                // instead of asking the player to repeat a non-idempotent
                // action blindly.
                next_poll = now;
            } else if (result.request.kind == RevivalSocialRequestKind::friend_action &&
                       (result.has_friends || !result.snapshot.friends.empty())) {
                connected = true;
                snapshot.friends = result.snapshot.friends;
                next_poll = now;
            } else if (result.snapshot.lobby.has_value() && older_lobby(result.snapshot.lobby)) {
                result.snapshot.lobby = snapshot.lobby;
                next_poll = now;
            } else if (result.snapshot.lobby.has_value()) {
                connected = true;
                snapshot.lobby = result.snapshot.lobby;
                next_poll = now + config.active_lobby_poll_interval;
            } else if (result.request.kind == RevivalSocialRequestKind::lobby_action &&
                       (result.has_lobby || result.request.action == "leave" || result.request.action == "close") &&
                       !result.snapshot.lobby.has_value()) {
                // A late acknowledgement from another lobby must not erase a
                // newer membership. Leave/Close responses are authoritative nulls.
                if (!snapshot.lobby || snapshot.lobby->id == result.request.lobby_id)
                    snapshot.lobby.reset();
                next_poll = now;
            }
            if (result && result.request.kind != RevivalSocialRequestKind::sync) {
                connected = true;
                last_error.clear();
                if (result.has_invitations) {
                    snapshot.invitations = result.snapshot.invitations;
                } else if (result.request.kind == RevivalSocialRequestKind::lobby_action &&
                           (result.request.action == "join" ||
                            result.request.action == "decline_invite")) {
                    const auto invitation = result.request.payload.find("invitation_id");
                    if (invitation != result.request.payload.end() && invitation->is_string()) {
                        const auto id = invitation->get<std::string>();
                        std::erase_if(snapshot.invitations, [&](const auto& row) {
                            return row.id == id;
                        });
                    }
                }
            }
            output.push_back(std::move(result));
        }
        drained.notify_all();
        return output;
    }

    [[nodiscard]] RevivalSocialSnapshot current_snapshot() const {
        std::scoped_lock lock{mutex};
        return snapshot;
    }

    [[nodiscard]] RevivalSocialClientStatus status(
        std::chrono::steady_clock::time_point now) const {
        std::scoped_lock lock{mutex};
        const auto retry = next_poll > now
                               ? std::chrono::duration_cast<std::chrono::milliseconds>(next_poll - now)
                               : std::chrono::milliseconds::zero();
        return RevivalSocialClientStatus{enabled && connected,
                                         closing,
                                         normal.active,
                                         priority.active,
                                         normal.queue.size(),
                                         priority.queue.size(),
                                         retry,
                                         last_error};
    }

    void shutdown(std::chrono::milliseconds grace) noexcept {
        std::unique_lock lock{mutex};
        if (closing) return;
        closing = true;
        if (enabled && grace > std::chrono::milliseconds::zero() &&
            !client_instance_id.empty() && priority.queue.size() < config.maximum_priority_requests) {
            RevivalSocialRequest request;
            request.generation = next_generation++;
            request.submission_sequence = next_submission_sequence++;
            request.kind = RevivalSocialRequestKind::presence_offline;
            request.priority = true;
            request.client_instance_id = client_instance_id;
            priority.queue.push_front(std::move(request));
            wake.notify_all();
            const auto deadline = std::chrono::steady_clock::now() + grace;
            static_cast<void>(drained.wait_until(lock, deadline, [this] {
                return !priority.active && priority.queue.empty();
            }));
        }
        normal.queue.clear();
        priority.queue.clear();
        lock.unlock();
        normal.worker.request_stop();
        priority.worker.request_stop();
        wake.notify_all();
    }

    void run(Lane& lane, std::stop_token stop) noexcept {
        while (!stop.stop_requested()) {
            RevivalSocialRequest request;
            {
                std::unique_lock lock{mutex};
                const auto ready = wake.wait(lock, stop, [&] { return !lane.queue.empty(); });
                if (!ready || stop.stop_requested()) return;
                request = std::move(lane.queue.front());
                lane.queue.pop_front();
                lane.active = true;
            }
            RevivalSocialResult result;
            const auto started = std::chrono::steady_clock::now();
            try {
                result = executor(request, stop);
            } catch (const std::exception& error) {
                result.request = request;
                result.error_code = "worker_exception";
                result.error = core::utf8_code_point_prefix(error.what(), 160U);
            } catch (...) {
                result.request = request;
                result.error_code = "worker_exception";
                result.error = "Social worker failed with an unknown exception.";
            }
            trace_result(result, std::chrono::steady_clock::now() - started);
            {
                std::scoped_lock lock{mutex};
                lane.active = false;
                if (!stop.stop_requested()) push_result_locked(std::move(result));
                drained.notify_all();
            }
        }
    }

    /**
     * Diagnostics for lobby support. Player actions are always recorded; the
     * frequent background sync only when it fails or recovers. Never logs
     * tokens or request payloads.
     */
    void trace_result(const RevivalSocialResult& result,
                      std::chrono::steady_clock::duration elapsed) {
        const bool sync = result.request.kind == RevivalSocialRequestKind::sync;
        const bool failed = !static_cast<bool>(result);
        if (sync) {
            const bool was_failing = sync_failing.exchange(failed);
            if (!failed && !was_failing) return;
            if (failed && was_failing) return;
        }
        std::string line{request_kind_name(result.request.kind)};
        if (!result.request.action.empty()) line += "/" + result.request.action;
        if (!result.request.lobby_id.empty()) line += " lobby=" + result.request.lobby_id;
        line += " http=" + std::to_string(result.http_status);
        line += " " + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + "ms";
        if (failed) {
            line += " FAILED code=" + (result.error_code.empty() ? std::string{"-"} : result.error_code);
            if (!result.error.empty()) line += " error=" + core::utf8_code_point_prefix(result.error, 160U);
        } else if (sync) {
            line += " recovered";
        }
        if (result.snapshot.lobby.has_value()) {
            const auto& lobby = *result.snapshot.lobby;
            line += " state=" + lobby.state + " rev=" + lobby.revision +
                    " members=" + std::to_string(lobby.members.size());
        }
        core::diagnostic("social", line);
    }

    [[nodiscard]] static std::string_view request_kind_name(RevivalSocialRequestKind kind) noexcept {
        switch (kind) {
        case RevivalSocialRequestKind::sync: return "sync";
        case RevivalSocialRequestKind::presence_offline: return "presence_offline";
        case RevivalSocialRequestKind::find_friends: return "find_friends";
        case RevivalSocialRequestKind::friend_action: return "friend_action";
        case RevivalSocialRequestKind::list_lobbies: return "list_lobbies";
        case RevivalSocialRequestKind::create_lobby: return "create_lobby";
        case RevivalSocialRequestKind::lobby_action: return "lobby_action";
        }
        return "unknown";
    }

    [[nodiscard]] bool poll_pending_locked() const noexcept {
        if (normal.active) return true;
        return std::ranges::any_of(normal.queue, [](const RevivalSocialRequest& request) {
            return request.kind == RevivalSocialRequestKind::sync;
        });
    }

    [[nodiscard]] bool older_lobby(const std::optional<RevivalSocialLobby>& incoming) const {
        if (!incoming || !snapshot.lobby || incoming->id != snapshot.lobby->id) return false;
        auto left = std::string_view{incoming->revision};
        auto right = std::string_view{snapshot.lobby->revision};
        const auto decimal = [](std::string_view value) {
            return !value.empty() && std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; });
        };
        if (!decimal(left) || !decimal(right)) return false;
        while (left.size() > 1U && left.front() == '0') left.remove_prefix(1U);
        while (right.size() > 1U && right.front() == '0') right.remove_prefix(1U);
        return left.size() < right.size() || (left.size() == right.size() && left < right);
    }

    void push_result_locked(RevivalSocialResult result) {
        if (results.size() >= config.maximum_results) {
            const auto stale_sync = std::ranges::find_if(results, [](const RevivalSocialResult& queued) {
                return queued.request.kind == RevivalSocialRequestKind::sync;
            });
            if (stale_sync != results.end()) {
                results.erase(stale_sync);
            } else {
                core::diagnostic("social", "result queue full: dropped an undelivered " +
                    std::string{request_kind_name(results.front().request.kind)} + " reply");
                results.pop_front();
            }
        }
        results.push_back(std::move(result));
    }

    /**
     * Authoritative events may be repeated until the next cursor is observed.
     * Delivering an invite/start event twice can open two loading screens or
     * accept an invitation that no longer exists, so retain a bounded process-
     * lifetime ID window and expose every event at most once.
     */
    void filter_new_events_locked(RevivalSocialSnapshot& incoming) {
        std::erase_if(incoming.events, [&](const RevivalSocialEvent& event) {
            if (seen_event_ids.contains(event.id)) return true;
            seen_event_ids.insert(event.id);
            seen_event_order.push_back(event.id);
            while (seen_event_order.size() > maximum_seen_events) {
                seen_event_ids.erase(seen_event_order.front());
                seen_event_order.pop_front();
            }
            return false;
        });
    }

    Executor executor;
    RevivalSocialClientConfig config;
    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::condition_variable_any drained;
    Lane normal;
    Lane priority;
    std::deque<RevivalSocialResult> results;
    RevivalSocialSnapshot snapshot;
    static constexpr std::size_t maximum_seen_events{1'024U};
    std::set<std::string, std::less<>> seen_event_ids;
    std::deque<std::string> seen_event_order;
    std::atomic_bool sync_failing{};
    std::string client_instance_id;
    std::string presence{"online"};
    Json metadata = Json::object();
    std::string last_error;
    std::uint64_t next_generation{1U};
    std::uint64_t next_submission_sequence{1U};
    std::uint64_t newest_priority_submission{};
    bool enabled{};
    bool connected{};
    bool closing{};
    std::chrono::steady_clock::time_point next_poll{};
    std::chrono::milliseconds backoff;
};

RevivalSocialClient::RevivalSocialClient(Executor executor,
                                         RevivalSocialClientConfig config)
    : impl_{std::make_unique<Impl>(std::move(executor), config)} {}

RevivalSocialClient::~RevivalSocialClient() = default;

bool RevivalSocialClient::enqueue(RevivalSocialRequest request) {
    return impl_->enqueue(std::move(request));
}

void RevivalSocialClient::set_available(bool available) noexcept {
    impl_->set_available(available);
}

void RevivalSocialClient::set_presence(std::string presence, nlohmann::json metadata) {
    impl_->set_presence(std::move(presence), std::move(metadata));
}

void RevivalSocialClient::tick(std::chrono::steady_clock::time_point now) { impl_->tick(now); }

std::vector<RevivalSocialResult> RevivalSocialClient::drain(
    std::chrono::steady_clock::time_point now) {
    return impl_->drain(now);
}

RevivalSocialSnapshot RevivalSocialClient::snapshot() const { return impl_->current_snapshot(); }

RevivalSocialClientStatus RevivalSocialClient::status(
    std::chrono::steady_clock::time_point now) const {
    return impl_->status(now);
}

void RevivalSocialClient::shutdown(std::chrono::milliseconds grace) noexcept {
    impl_->shutdown(grace);
}

} // namespace battlespades::network
