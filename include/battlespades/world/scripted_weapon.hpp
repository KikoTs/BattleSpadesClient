#pragma once
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/class_models.hpp"
#include "battlespades/world/weapon_variants.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::world {
enum class SkinArmPart { none, lower, upper };
struct SkinResource final {
    std::string name;
    std::filesystem::path path;
    int kind{};
    SkinArmPart arm_part{SkinArmPart::none};
};
struct SkinModelDraw final { std::size_t resource{}; std::array<float,16> transform{}; };
struct SkinSprite final {
    std::size_t resource{};
    std::array<float,3> position{};
    std::array<float,4> color{};
    float radius{}, rotation{};
    bool screen{};
};
struct SkinSound final { std::size_t resource{}; float gain{1}, pitch{1}; };
struct SkinInput final {
    float dt{}, aim{}, sprint{}, raise{1}, ready{1}, reload_progress{1};
    bool reloading{}, fired{}, reload_started{}, reload_finished{}, muted{};
    int ammo{30}, clip_size{30};
    std::array<float,3> swing{}, team_color{.3F,.6F,1.F};
    float screen_width{1280}, screen_height{720};
};
struct SkinFrame final {
    std::vector<SkinModelDraw> models;
    std::vector<SkinSprite> sprites;
    std::vector<SkinSound> sounds;
    std::array<float,3> left_hand{}, right_hand{};
    float scope_opacity{};
};
/** Converts classic movement offsets to the smaller OpenSpades presentation range. */
class ScriptedWeaponMotion final {
public:
    void apply(SkinInput& input, bool enabled);
private:
    float sprint_{};
    std::array<float,3> swing_{};
};
/** Executes only the skin presentation API; no filesystem, process or network API is exposed. */
class ScriptedWeapon final {
public:
    ScriptedWeapon();
    ~ScriptedWeapon();
    ScriptedWeapon(const ScriptedWeapon&)=delete;
    ScriptedWeapon& operator=(const ScriptedWeapon&)=delete;
    [[nodiscard]] bool load(const std::filesystem::path& manifest, std::string& error,
                            const SkinVariantSelection& selection = {});
    [[nodiscard]] const ResolvedSkinVariant& variant() const;
    [[nodiscard]] bool update(const SkinInput& input, std::string& error);
    [[nodiscard]] const std::vector<SkinResource>& resources() const;
    [[nodiscard]] const SkinFrame& frame() const;
    /** Character overrides have already passed the inventory asset hash checks. */
    [[nodiscard]] ChunkMesh model_mesh(std::size_t resource, VxlColor team,
                                      const ClassModelOverrides& character = {}) const;
    [[nodiscard]] static ChunkMesh source_mesh(const std::filesystem::path& path,std::optional<VxlColor> team=std::nullopt);
    [[nodiscard]] static ChunkMesh source_mesh(Kv6Model model,std::optional<VxlColor> team=std::nullopt);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace battlespades::world
