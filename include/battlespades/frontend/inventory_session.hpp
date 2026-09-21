#pragma once
#include "battlespades/frontend/inventory_menu.hpp"
#include "battlespades/network/revival_identity.hpp"
#include "battlespades/world/weapon_models.hpp"
#include "battlespades/world/class_models.hpp"
#include "battlespades/world/weapon_variants.hpp"
#include <filesystem>
#include <future>
#include <memory>
#include <stop_token>

namespace battlespades::frontend {
[[nodiscard]] std::optional<world::ChunkMesh> inventory_preview_mesh(
    const InventoryCosmetic& item, const std::filesystem::path& root, bool blue_team,
    std::optional<std::uint8_t> class_id = std::nullopt, const InventoryCosmetic* hat = nullptr,
    bool head_only = false);
[[nodiscard]] std::optional<world::ChunkMesh> inventory_weapon_preview_mesh(
    const InventoryCosmetic& item, const std::filesystem::path& root, bool blue_team,
    const world::SkinVariantSelection& variants);
[[nodiscard]] const InventoryCosmetic* find_inventory_cosmetic(std::string_view id);
[[nodiscard]] std::shared_ptr<const world::Kv6Model> inventory_verified_model(
    const InventoryCosmetic& item, const std::filesystem::path& root);
[[nodiscard]] world::ClassModelOverrides inventory_character_parts(const InventoryCosmetic* item,const std::filesystem::path& root);
[[nodiscard]] std::optional<world::WeaponCosmeticFinish> inventory_weapon_finish(
    const InventoryCosmetic* item, const std::filesystem::path& root, std::uint8_t tool);
[[nodiscard]] InventoryData parse_inventory_snapshot(const nlohmann::json& value);
[[nodiscard]] InventoryData inventory_preview_fixture();
[[nodiscard]] bool verify_inventory_receipt(const nlohmann::json& receipt,
                                            const InventoryData& before);
[[nodiscard]] std::vector<std::uint8_t> build_inventory_preview(const InventoryCosmetic& item,
                                                                const std::filesystem::path& root,
                                                                bool blue_team,
                                                                double angle,
                                                                double zoom);
class InventorySession final {
public:
    explicit InventorySession(std::shared_ptr<network::RevivalIdentityService> identity);
    ~InventorySession();
    void start(InventoryMenuModel& model, InventoryAction action);
    void pump(InventoryMenuModel& model);
    void cancel() noexcept;

private:
    struct Outcome {
        std::optional<InventoryData> data;
        std::optional<InventoryOpening> receipt;
        std::string error;
        bool offline{};
        std::string account;
    };
    std::shared_ptr<network::RevivalIdentityService> identity_;
    std::future<Outcome> worker_;
    std::stop_source stop_;
};
} // namespace battlespades::frontend
