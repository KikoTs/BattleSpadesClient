#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace battlespades::world {

struct VxlColor final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255U};

    [[nodiscard]] friend constexpr bool operator==(const VxlColor&,
                                                   const VxlColor&) = default;
};

struct VxlLoadResult;

/**
 * Canonical Battle Builder voxel map.
 *
 * The decoder accepts raw, decompressed VXL bytes. Network framing and zlib
 * belong to the Protocol 168 transport adapter; keeping them outside this type
 * lets disk Tutorial maps and server MapSync data use the identical parser.
 */
class VxlMap final {
public:
    static constexpr std::uint32_t width{512U};
    static constexpr std::uint32_t depth{512U};
    static constexpr std::uint32_t height{240U};

    [[nodiscard]] static VxlLoadResult load(std::span<const std::byte> bytes);
    [[nodiscard]] static VxlLoadResult load_file(const std::filesystem::path& path);

    [[nodiscard]] bool solid(std::uint32_t x, std::uint32_t y,
                             std::uint32_t z) const noexcept;
    [[nodiscard]] std::optional<VxlColor> color(std::uint32_t x, std::uint32_t y,
                                                std::uint32_t z) const noexcept;
    [[nodiscard]] std::uint16_t surface_z(std::uint32_t x,
                                          std::uint32_t y) const noexcept;
    [[nodiscard]] std::uint64_t solid_voxels() const noexcept;
    [[nodiscard]] std::uint32_t source_edge() const noexcept;
    [[nodiscard]] std::uint32_t source_z_shift() const noexcept;

    /**
     * Places one solid voxel, replacing any existing color. Fails closed for
     * out-of-range coordinates. Successful mutations advance revision() so
     * chunk meshes and future MapSync consumers can invalidate precisely.
     */
    [[nodiscard]] bool set_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                 VxlColor color) noexcept;

    /**
     * Removes one solid voxel and repairs the cached column surface. Fails
     * closed for out-of-range or already-empty coordinates and for the forced
     * z=239 bed, which Battle Builder never exposes to destruction.
     */
    [[nodiscard]] bool clear_voxel(std::uint32_t x, std::uint32_t y,
                                   std::uint32_t z) noexcept;

    /**
     * Visual damage fraction for a solid voxel, in [0,1). Protocol 168
     * Damage(37) changes this before the block is removed; chunk meshing uses
     * it to reproduce the retail pre-break darkening.
     */
    [[nodiscard]] float damage_fraction(std::uint32_t x, std::uint32_t y,
                                        std::uint32_t z) const noexcept;
    /** Sets or clears pre-break visual damage. Invalid/air/bed cells fail closed. */
    [[nodiscard]] bool set_damage_fraction(std::uint32_t x, std::uint32_t y,
                                           std::uint32_t z, float fraction) noexcept;
    /** Clears pre-break damage, returning true only when state changed. */
    [[nodiscard]] bool clear_damage(std::uint32_t x, std::uint32_t y,
                                    std::uint32_t z) noexcept;

    /** Monotonic mutation counter; load produces revision zero. */
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    std::vector<std::uint8_t> solid_bits_;
    std::vector<std::uint32_t> colors_;
    std::vector<std::uint16_t> surfaces_;
    std::unordered_map<std::uint32_t, float> damage_;
    std::uint64_t solid_voxels_{};
    std::uint64_t revision_{};
    std::uint32_t source_edge_{};
    std::uint32_t source_z_shift_{};

    void put(std::uint32_t x, std::uint32_t y, std::uint32_t z,
             std::uint32_t color) noexcept;
};

struct VxlLoadResult final {
    std::optional<VxlMap> map;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return map.has_value(); }
};

} // namespace battlespades::world
