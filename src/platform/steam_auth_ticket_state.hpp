#pragma once

#include "battlespades/platform/steam_networking.hpp"

#include <map>
#include <span>
#include <utility>

namespace battlespades::platform::detail {

enum class SteamTicketKind { web_api, session };

/** Bounded callback bookkeeping. The runtime holds its mutex for every call. */
class SteamAuthTicketState final {
public:
    static constexpr std::size_t maximum_live_tickets{8U};
    static constexpr std::size_t maximum_session_bytes{1'016U};
    static constexpr std::size_t maximum_web_api_bytes{2'560U};

    struct Entry final {
        SteamTicketKind kind{};
        std::uint32_t app_id{};
        std::uint64_t steam_id{};
        std::string hex;
        std::string error;
        bool complete{};
        bool taken{};
    };

    [[nodiscard]] bool full() const noexcept { return entries_.size() >= maximum_live_tickets; }

    [[nodiscard]] bool begin(std::uint32_t handle, SteamTicketKind kind,
                             std::uint32_t app_id, std::uint64_t steam_id,
                             std::span<const std::byte> session = {}) {
        if (handle == 0U || app_id == 0U || steam_id == 0U || full() ||
            (kind == SteamTicketKind::session &&
             (app_id != 224540U || session.empty() || session.size() > maximum_session_bytes))) {
            return false;
        }
        Entry entry{kind, app_id, steam_id, {}, {}, false, false};
        if (kind == SteamTicketKind::session) {
            // The original wrapper prefixes the raw ticket with CSteamID.
            for (unsigned shift{}; shift < 64U; shift += 8U) {
                append_hex(entry.hex, static_cast<unsigned char>((steam_id >> shift) & 0xffU));
            }
            for (auto value : session) append_hex(entry.hex, std::to_integer<unsigned char>(value));
        }
        return entries_.emplace(handle, std::move(entry)).second;
    }

    void complete(std::uint32_t handle, SteamTicketKind kind, bool success,
                  std::span<const std::byte> bytes = {}) {
        const auto found = entries_.find(handle);
        if (found == entries_.end() || found->second.kind != kind || found->second.complete) return;
        auto& entry = found->second;
        entry.complete = true;
        if (!success || (kind == SteamTicketKind::web_api &&
                         (bytes.empty() || bytes.size() > maximum_web_api_bytes))) {
            entry.hex.clear();
            entry.error = "Steam could not issue an authentication ticket";
        } else if (kind == SteamTicketKind::web_api) {
            entry.hex.reserve(bytes.size() * 2U);
            for (auto value : bytes) append_hex(entry.hex, std::to_integer<unsigned char>(value));
        }
    }

    [[nodiscard]] std::optional<Entry> take(std::uint32_t handle, SteamTicketKind kind) {
        const auto found = entries_.find(handle);
        if (found == entries_.end() || found->second.kind != kind ||
            !found->second.complete || found->second.taken) return std::nullopt;
        auto& entry = found->second;
        entry.taken = true;
        Entry result = entry;
        entry.hex.clear();
        return result;
    }

    [[nodiscard]] bool cancel(std::uint32_t handle) { return entries_.erase(handle) != 0U; }
    [[nodiscard]] const auto& entries() const noexcept { return entries_; }

private:
    static void append_hex(std::string& result, unsigned char byte) {
        constexpr char digits[]{"0123456789abcdef"};
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 15U]);
    }
    std::map<std::uint32_t, Entry> entries_;
};

} // namespace battlespades::platform::detail
