#pragma once

#include "battlespades/network/aosplay_scores.hpp"
#include "battlespades/network/revival_social.hpp"
#include "battlespades/network/revival_inventory.hpp"
#include "battlespades/network/public_workshop.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <utility>
#include <vector>

namespace battlespades::network {

struct RevivalAccount final {
    std::string public_id{};
    std::string legacy_id{};
    std::string nickname{};
    std::string account_type{};
    std::string identity_type{};
    bool ranked_eligible{};
    bool offline{};
    /** Provider names are presentation only; public_id/SteamID identify the account. */
    std::string steam_id{};
    std::string registered_name{};
    std::string display_name{};
};

struct RevivalAuthResult final {
    std::optional<RevivalAccount> account{};
    std::string recovery_code{};
    std::string error_code{};
    std::string error{};
    long http_status{};
    std::filesystem::path recovery_backup_path{};
    std::string recovery_backup_error{};

    RevivalAuthResult() = default;
    explicit RevivalAuthResult(std::optional<RevivalAccount> selected_account)
        : account{std::move(selected_account)} {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return account.has_value() && error.empty();
    }
};

struct RevivalTicketResult final {
    std::string join_code{};
    std::string error_code{};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return join_code.size() == 15U && join_code.front() == '~' &&
               error.empty();
    }
};

struct HostedResultsUpload final {
    std::size_t uploaded{};
    std::string error{};
};

/** Authenticated AoSPlay allocation used by one client-owned public match. */
struct RevivalRelayLobby final {
    std::string lobby_id{};
    std::string server_id{};
    std::string server_token{};
    std::string master_url{};
    std::string allocation_id{};
    std::string relay_host{};
    std::uint16_t relay_port{};
    std::string host_key{};
    std::uint16_t keepalive_seconds{10U};
};

struct RevivalRelayLobbyResult final {
    std::optional<RevivalRelayLobby> lobby{};
    std::string error_code{};
    std::string error{};
    long http_status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return lobby.has_value() && error.empty();
    }
};

struct RevivalRelayLobbyRequest final {
    std::string name{};
    std::string map{};
    std::string game_mode{};
    std::string mode_tla{};
    std::uint16_t max_players{12U};
    std::uint16_t playlist_id{};
    std::string texture_skin{};
    bool classic{};
    /**
     * The host's Steam id, so a player browsing the list can reach the match
     * over Valve's relays instead of the AoSPlay one. Empty when the host has
     * no Steam session, which keeps the relay the only route.
     */
    std::string steam_host_id{};
};

struct RevivalIdentityConfig final {
    std::string api_base{"https://www.aosplay.net"};
    std::filesystem::path state_path;
    std::chrono::milliseconds timeout{5'000};
    std::size_t maximum_payload_bytes{64U * 1'024U};
    /** Explicit offline sessions use their own state file and never perform HTTP. */
    bool offline{};
    std::string offline_profile{"Player"};
    bool allow_environment_override{true};
    /** Empty selects the user's Documents/BattleSpades directory. */
    std::filesystem::path recovery_directory{};
};

struct RevivalWorkshopFile final {
    std::string filename{};
    std::string content_type{};
    std::string kind{};
    std::string sha256{};
    std::string modified_ticks{};
    std::vector<unsigned char> bytes{};
};

struct RevivalWorkshopProject final {
    std::vector<RevivalWorkshopFile> files{};
    std::string description{};
    std::string author{};
    std::vector<std::string> tags{};
};

/** Freeze bounded project siblings; reject traversal, links and partial reads. */
[[nodiscard]] RevivalWorkshopProject read_revival_workshop_project(
    const std::filesystem::path& maps_root, std::string_view uid);

struct RevivalWorkshopResult final {
    std::string item_url{};
    std::string error{};
    std::string warning{};
    [[nodiscard]] explicit operator bool() const noexcept { return !item_url.empty() && error.empty(); }
};

/** Returns `%LOCALAPPDATA%/AoS Revival/launcher_state.json` where possible. */
[[nodiscard]] std::filesystem::path default_revival_state_path();

/** Client-side preflight matching the public AoSPlay registration contract. */
[[nodiscard]] bool valid_revival_username(std::string_view username) noexcept;
[[nodiscard]] bool valid_revival_registration_password(
    std::string_view password,
    std::string_view username) noexcept;

/**
 * Thread-safe AoSPlay account client and protected launcher-state owner.
 *
 * Calls perform blocking network/file work and belong on a worker thread.
 * Secrets are encrypted with Windows DPAPI in the exact `launcher_state.json`
 * format used by the Python launcher. No method returns the bearer token or
 * private guest seed.
 */
class RevivalIdentityService final {
public:
    explicit RevivalIdentityService(RevivalIdentityConfig config = {});
    ~RevivalIdentityService();

    RevivalIdentityService(const RevivalIdentityService&) = delete;
    RevivalIdentityService& operator=(const RevivalIdentityService&) = delete;
    RevivalIdentityService(RevivalIdentityService&&) noexcept;
    RevivalIdentityService& operator=(RevivalIdentityService&&) noexcept;

    [[nodiscard]] std::optional<RevivalAccount> cached_account() const;
    [[nodiscard]] bool has_online_session() const;

    [[nodiscard]] RevivalAuthResult refresh();
    [[nodiscard]] RevivalAuthResult login(std::string username,
                                          std::string password);
    [[nodiscard]] RevivalAuthResult register_account(std::string username,
                                                     std::string password);
    [[nodiscard]] RevivalAuthResult guest_login();
    /** Exchange a fresh Steam WebAPI ticket; never infer identity from a nickname. */
    [[nodiscard]] RevivalAuthResult steam_login(std::uint32_t app_id,
                                                std::string ticket_hex,
                                                std::string expected_steam_id,
                                                bool link_existing = false);
    [[nodiscard]] RevivalAuthResult recover_steam_account(std::string steam_id,
                                                          std::string recovery_code);
    [[nodiscard]] RevivalAuthResult logout();
    [[nodiscard]] RevivalTicketResult game_ticket(std::string server_id);
    /** Account-scoped, credential-free reports survive disposable host folders. */
    [[nodiscard]] std::filesystem::path hosted_results_directory() const;
    /** Bounded worker operation; remove a report only after a committed acknowledgement. */
    [[nodiscard]] HostedResultsUpload flush_hosted_results(std::stop_token stop = {});
    /** Allocate a NAT-free public UDP endpoint for one owned local server. */
    [[nodiscard]] RevivalRelayLobbyResult create_relay_lobby(
        const RevivalRelayLobbyRequest& request,
        std::stop_token stop = {});
    /** Best-effort release using the allocation-scoped server credential. */
    [[nodiscard]] bool close_relay_lobby(const RevivalRelayLobby& lobby,
                                         std::stop_token stop = {});
    /** Fetch the signed-in account's profile without exposing its bearer token. */
    [[nodiscard]] AosPlayProfileResult own_profile();
    /** Bounded collection operations; the account bearer never leaves this service. */
    [[nodiscard]] InventoryResult inventory_request(const InventoryRequest& request,
                                                   std::stop_token stop = {});
    /** Execute one typed social request without exposing the bearer token. */
    [[nodiscard]] RevivalSocialResult social_request(
        const RevivalSocialRequest& request,
        std::stop_token stop = {});
    /** Publishes only after all immutable file checksums are verified by AoSPlay. */
    [[nodiscard]] RevivalWorkshopResult publish_ugc_project(
        const std::filesystem::path& maps_root, std::string_view uid,
        std::string_view title, std::stop_token stop = {});

    [[nodiscard]] WorkshopResult workshop_request(const WorkshopRequest& request,
                                                   std::stop_token stop = {});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::network
