#pragma once

#include "battlespades/network/aosplay_scores.hpp"
#include "battlespades/network/revival_social.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <utility>

namespace battlespades::network {

struct RevivalAccount final {
    std::string public_id;
    std::string legacy_id;
    std::string nickname;
    std::string account_type;
    std::string identity_type;
    bool ranked_eligible{};
    bool offline{};
};

struct RevivalAuthResult final {
    std::optional<RevivalAccount> account;
    std::string recovery_code;
    std::string error_code;
    std::string error;
    long http_status{};

    RevivalAuthResult() = default;
    explicit RevivalAuthResult(std::optional<RevivalAccount> selected_account)
        : account{std::move(selected_account)} {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return account.has_value() && error.empty();
    }
};

struct RevivalTicketResult final {
    std::string join_code;
    std::string error_code;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return join_code.size() == 15U && join_code.front() == '~' &&
               error.empty();
    }
};

struct RevivalIdentityConfig final {
    std::string api_base{"https://www.aosplay.net"};
    std::filesystem::path state_path;
    std::chrono::milliseconds timeout{5'000};
    std::size_t maximum_payload_bytes{64U * 1'024U};
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
    [[nodiscard]] RevivalAuthResult logout();
    [[nodiscard]] RevivalTicketResult game_ticket(std::string server_id);
    /** Fetch the signed-in account's profile without exposing its bearer token. */
    [[nodiscard]] AosPlayProfileResult own_profile();
    /** Execute one typed social request without exposing the bearer token. */
    [[nodiscard]] RevivalSocialResult social_request(
        const RevivalSocialRequest& request,
        std::stop_token stop = {});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::network
