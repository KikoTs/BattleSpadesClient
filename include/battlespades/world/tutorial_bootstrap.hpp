#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/emissive_volume.hpp"
#include "battlespades/world/skylight_map.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::world {

enum class BootstrapStage : std::uint8_t {
    idle,
    parsing,
    meshing,
    ready,
    failed,
};

struct BootstrapProgress final {
    BootstrapStage stage{BootstrapStage::idle};
    /** Monotonic 0..1 across parse and mesh work for the loading screen. */
    double fraction{};
    std::size_t chunks_meshed{};
    std::size_t chunks_total{};
    std::string error;
};

/**
 * Renderer-neutral map products prepared beside chunk meshing.
 *
 * They are pure functions of the immutable bootstrap map, so calculating them
 * on the loader thread avoids a large synchronous spike when START/class
 * selection reveals the world. `map_revision` lets later terrain catch-up
 * refresh the small affected regions before the GPU upload.
 */
struct BootstrapDerivedWorld final {
    SkylightMap skylight;
    EmissiveVolume emissive;
    std::vector<std::uint8_t> minimap_rgba;
    std::uint64_t map_revision{};
};

/**
 * Bounded background loader for the Tutorial world.
 *
 * A coordinator thread parses the raw VXL, then a small worker pool meshes
 * all chunks against the shared immutable map. Finished meshes accumulate in
 * a size-capped queue: workers block when the render thread falls behind on
 * uploads, so peak memory stays bounded instead of buffering the whole world.
 * The main thread never blocks; it polls progress(), drains ready meshes
 * with an upload budget and owns all GPU work. cancel() (or destruction)
 * stops the pipeline promptly, letting Escape abandon a load safely.
 */
class TutorialWorldBootstrap final {
public:
    explicit TutorialWorldBootstrap(std::filesystem::path vxl_path,
                                    std::uint32_t worker_count = 0U);
    /** Mesh an already validated network MapSync without reparsing/copying. */
    explicit TutorialWorldBootstrap(std::shared_ptr<VxlMap> map,
                                    std::string map_name = {},
                                    std::uint32_t worker_count = 0U);
    ~TutorialWorldBootstrap();

    TutorialWorldBootstrap(const TutorialWorldBootstrap&) = delete;
    TutorialWorldBootstrap& operator=(const TutorialWorldBootstrap&) = delete;
    TutorialWorldBootstrap(TutorialWorldBootstrap&&) = delete;
    TutorialWorldBootstrap& operator=(TutorialWorldBootstrap&&) = delete;

    void start();
    void cancel() noexcept;

    [[nodiscard]] BootstrapProgress progress() const;
    [[nodiscard]] std::vector<ChunkMesh> take_ready_meshes(std::size_t limit);
    /** Non-null once parsing has completed successfully; the session takes
     * mutable ownership for digging/building once loading finishes. */
    [[nodiscard]] std::shared_ptr<VxlMap> map() const;
    /** Consumes the precomputed skylight, emissive spill and minimap once. */
    [[nodiscard]] std::optional<BootstrapDerivedWorld> take_derived_world();

private:
    struct State;
    std::shared_ptr<State> state_;
    std::filesystem::path path_;
    std::shared_ptr<VxlMap> supplied_map_;
    std::string map_name_;
    std::uint32_t worker_count_;
};

} // namespace battlespades::world
