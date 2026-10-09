#pragma once

#include "battlespades/world/chunk_mesh.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::world {

/**
 * One retail KV6 voxel model (SLAB6 layout, verified byte-exact against the
 * shipped kv6/ assets: "Kvxl" header, per-voxel 8-byte records ordered by
 * x-major columns, u32 xlen and u16 ylen run tables, optional trailing
 * "SPal" palette which retail ignores).
 */
class Kv6Model final {
public:
    struct Voxel final {
        std::uint16_t x{};
        std::uint16_t y{};
        std::uint16_t z{};
        VxlColor color{};
        std::uint8_t visibility{};
        std::uint8_t normal_index{};
    };

    [[nodiscard]] static std::optional<Kv6Model> load(std::span<const std::byte> bytes,
                                                      std::string* error = nullptr);
    [[nodiscard]] static std::optional<Kv6Model>
    load_file(const std::filesystem::path& path, std::string* error = nullptr);

    [[nodiscard]] std::uint32_t size_x() const noexcept { return size_x_; }
    [[nodiscard]] std::uint32_t size_y() const noexcept { return size_y_; }
    [[nodiscard]] std::uint32_t size_z() const noexcept { return size_z_; }
    [[nodiscard]] const std::array<float, 3U>& pivot() const noexcept { return pivot_; }
    [[nodiscard]] const std::vector<Voxel>& voxels() const noexcept { return voxels_; }
    [[nodiscard]] float voxel_scale() const noexcept { return voxel_scale_; }

    /**
     * Reproduce retail kv6.scale_kv6 for model-quality inverse scales 1..3.
     *
     * Occupied source voxels are grouped into N-cubed cells, RGB is averaged
     * with integer division, team-colour marker groups remain black, dimensions
     * are ceiling-divided, and pivots are divided by N. The mesher retains the
     * original world-space bounds by drawing each result voxel at size N.
     */
    [[nodiscard]] Kv6Model inverse_scaled(std::uint8_t inverse_scale) const;

    /** Adapt the classic 12x10x6 combined arm pose into upper/lower segments.
     * Uses the authored sleeve/glove voxels; returns empty for other layouts. */
    [[nodiscard]] std::vector<Kv6Model> articulated_classic_arms() const;
    /**
     * The half of the model above (`upper`) or below the middle of its height,
     * for a one-piece limb that must bend there. KV6 stores a hollow shell, so
     * the cut is capped: each half shows a closed end, not an open ring.
     */
    [[nodiscard]] Kv6Model split_at_middle_z(bool upper) const;

    /**
     * Apply retail KV6.offset_pivots in authored voxel units.
     *
     * The compiled method divides every authored offset by invscale before it
     * adds it to the already-downsampled pivot. This loader stores pivots in
     * scaled voxel coordinates, so it performs that division here. It is not a
     * world-space entity translation.
     */
    void offset_pivots(std::array<float, 3U> offset) noexcept;

    /**
     * Meshes the model into the shared chunk-vertex format, one shaded cube
     * face per exposed voxel side, positions in voxel units relative to the
     * authored pivot so the renderer's model transform owns placement.
     */
    /**
     * Resolves retail's team-controlled KV6 materials against a server-selected
     * RGB. Exact black and mid-magenta are the base band; dark/high magenta are
     * drawn at 0.7 and 1.3 intensity. This rule belongs to every KV6 display,
     * including shared crouch meshes and graves, rather than selected classes.
     */
    void apply_default_color(VxlColor team_color) noexcept;
    /** Render-only finish; preserves every voxel, pivot and team material marker. */
    void apply_cosmetic_palette(std::array<std::uint8_t, 3U> palette) noexcept;

    /**
     * Cube-meshes the surface voxels into the recovered retail GL model
     * space: vertex = (x - pivot_x, -(z - pivot_z), y - pivot_y), the
     * Rx(+90 degree) proper rotation Character.draw_fps assumes, so draw
     * transforms can be applied verbatim. tint modulates every voxel color
     * (arms are team-tinted in retail).
     *
     * Each cube is CENTRED on that coordinate, spanning it plus and minus half
     * a voxel, because kv6.pyd's own vertex writer is (see the citation in
     * kv6_model.cpp). Spanning [coord, coord + 1] instead offsets every model
     * in the game by half a voxel on all three axes.
     */
    [[nodiscard]] ChunkMesh mesh(const ChunkMesherConfig& shading = {},
                                 std::array<float, 3U> tint = {1.0F, 1.0F,
                                                               1.0F}) const;

private:
    std::uint32_t size_x_{};
    std::uint32_t size_y_{};
    std::uint32_t size_z_{};
    std::array<float, 3U> pivot_{};
    float voxel_scale_{1.0F};
    std::vector<Voxel> voxels_;
};

} // namespace battlespades::world
