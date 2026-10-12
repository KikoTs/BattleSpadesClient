#pragma once

#include "battlespades/world/classic_corpse.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include <array>
#include <span>

namespace battlespades::world {

/** Mesh-local, bounded cosmetic damage. Never used by gameplay collision. */
class BodyDamage final {
public:
    struct Surface {
        const ChunkMesh* mesh{};
        ClassicCorpse::Matrix transform{};
        std::uint8_t group{};
    };
    struct Chip { Vec3 position; std::uint32_t color{}, seed{}; bool bone{}; };
    struct Chips {
        std::array<Chip,16> values{};
        std::size_t count{};
        explicit operator bool() const noexcept { return count!=0; }
        auto begin() const { return values.begin(); }
        auto end() const { return values.begin()+count; }
    };
    void hit(Vec3 position, std::uint32_t seed, double radius=.095, std::uint8_t group=255,
             std::uint8_t intensity=1, bool cut=false);
    /** Resolve new contacts once against the displayed native model. */
    [[nodiscard]] Chips resolve(std::span<const Surface> surfaces);
    [[nodiscard]] ChunkMesh mesh(const ChunkMesh& source, std::uint8_t group) const;
    [[nodiscard]] bool marked(std::uint8_t group) const noexcept;
    [[nodiscard]] std::uint32_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::uint32_t revision(std::uint8_t group) const noexcept {
        return group < group_revisions_.size() ? group_revisions_[group] : 0;
    }
    [[nodiscard]] std::size_t wound_count() const noexcept { return count_; }
private:
    struct Wound { Vec3 point; double radius{}; std::uint8_t group{}; std::uint32_t seed{};
                   std::uint8_t intensity{}, face{}; bool cut{}; };
    struct Contact { Vec3 point; std::uint32_t seed{}; double radius{}; std::uint8_t group{};
                     std::uint8_t intensity{}; bool cut{}; };
    std::array<Wound, 32> wounds_{};
    std::array<Contact, 16> pending_{};
    std::size_t count_{}, pending_count_{}, next_{};
    std::uint32_t revision_{};
    std::array<std::uint32_t,8> group_revisions_{};
};

class ParticleSystem;
/** Fixed-burst falling skin and bone debris in the shared particle pool. */
void emit_body_fragments(ParticleSystem& particles, const BodyDamage::Chips& chips, std::uint8_t intensity);

enum class GoreWeapon { ordinary, shotgun, sniper, explosive };
struct GoreProfile {
    double wound_radius;
    unsigned fragments;
};
[[nodiscard]] GoreProfile gore_profile(std::uint8_t intensity) noexcept;
/** Artistic probabilities, restricted to lethal impacts or existing corpses. */
[[nodiscard]] bool sever_on_impact(GoreWeapon weapon, ClassicCorpse::Part part,
                                  double distance, bool lethal, std::uint32_t seed,
                                  std::uint8_t intensity=1) noexcept;
} // namespace battlespades::world
