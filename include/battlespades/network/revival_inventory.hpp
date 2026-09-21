#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace battlespades::network {

enum class InventoryRequestKind : std::uint8_t { snapshot, open, equip, unequip, crates, history };
struct InventoryRequest final {
    InventoryRequestKind kind{InventoryRequestKind::snapshot};
    std::string target;
    std::string cosmetic_id;
    std::string revision;
    std::string idempotency_key;
    std::string nonce;
    // Pin an asynchronous operation (including retries) to its original account.
    // Checked under the same lock that snapshots the bearer token; never sent.
    std::string expected_account;
};
struct InventoryResult final {
    nlohmann::json payload;
    std::string error;
    std::string error_code;
    long http_status{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty() && payload.is_object();
    }
};

} // namespace battlespades::network
