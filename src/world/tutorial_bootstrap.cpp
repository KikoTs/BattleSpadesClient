#include "battlespades/world/tutorial_bootstrap.hpp"

#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/minimap_overview.hpp"
#include "battlespades/world/static_light_field.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace battlespades::world {
namespace {

constexpr std::size_t mesh_queue_capacity{64U};
/** Parsing counts as this share of the loading bar; meshing fills the rest. */
constexpr double parse_fraction{0.25};

} // namespace

struct TutorialWorldBootstrap::State final {
    mutable std::mutex mutex;
    std::condition_variable queue_space;
    BootstrapStage stage{BootstrapStage::idle};
    std::string error;
    std::shared_ptr<VxlMap> map;
    std::optional<BootstrapDerivedWorld> derived_world;
    std::deque<ChunkMesh> ready;
    std::size_t chunks_meshed{};
    std::size_t chunks_total{};
    std::atomic<std::uint32_t> next_chunk{};
    std::atomic<bool> cancelled{};
    std::thread coordinator;
};

TutorialWorldBootstrap::TutorialWorldBootstrap(std::filesystem::path vxl_path,
                                               std::uint32_t worker_count)
    : state_{std::make_shared<State>()}, path_{std::move(vxl_path)}, worker_count_{worker_count} {}

TutorialWorldBootstrap::TutorialWorldBootstrap(std::shared_ptr<VxlMap> map,
                                               std::string map_name,
                                               std::uint32_t worker_count)
    : state_{std::make_shared<State>()}, supplied_map_{std::move(map)},
      map_name_{std::move(map_name)}, worker_count_{worker_count} {}

TutorialWorldBootstrap::~TutorialWorldBootstrap() {
    cancel();
    if (state_->coordinator.joinable()) {
        state_->coordinator.join();
    }
}

void TutorialWorldBootstrap::start() {
    {
        const std::lock_guard lock{state_->mutex};
        if (state_->stage != BootstrapStage::idle) {
            return;
        }
        state_->stage = BootstrapStage::parsing;
    }

    auto workers = worker_count_;
    if (workers == 0U) {
        const auto hardware = std::thread::hardware_concurrency();
        workers = hardware > 2U ? hardware - 2U : 1U;
    }
    workers = std::clamp(workers, 1U, 16U);

    auto state = state_;
    auto path = path_;
    auto supplied_map = supplied_map_;
    // The emissive palette is keyed on the map's basename, e.g. `TokyoNeon`.
    const std::string map_name = map_name_.empty() ? path_.stem().string() : map_name_;
    const auto bed_water_color = bed_water_color_;
    const bool retail_look = retail_look_;
    state_->coordinator = std::thread{[state, path, supplied_map, workers, map_name,
                                       bed_water_color, retail_look]() {
        std::shared_ptr<VxlMap> shared_map = supplied_map;
        if (shared_map == nullptr) {
            auto loaded = VxlMap::load_file(path);
            if (state->cancelled.load()) {
                return;
            }
            if (!loaded) {
                const std::lock_guard lock{state->mutex};
                state->stage = BootstrapStage::failed;
                state->error = loaded.error;
                return;
            }
            shared_map = std::make_shared<VxlMap>(std::move(*loaded.map));
        }
        if (state->cancelled.load()) {
            return;
        }
        // A map with authored light-emitting colours needs them classified at
        // mesh time, because the emission strength rides in the vertex colour.
        // The Retail tier meshes without it: vxl.pyd had no self-lit voxels.
        // The emissive spill volume below is still built for the enhanced
        // tiers, so a later tier switch only has to re-mesh.
        const auto map_palette = emissive_palette_for(map_name);
        ChunkMesherConfig mesher_config;
        if (!retail_look) {
            mesher_config.emissive = map_palette;
        }
        if (bed_water_color.has_value()) {
            mesher_config.bed_water_color = *bed_water_color;
        }
        const ChunkMesher mesher{mesher_config};
        const auto chunks_per_axis = mesher.chunks_per_axis();
        const auto total = static_cast<std::size_t>(chunks_per_axis) * chunks_per_axis;
        {
            const std::lock_guard lock{state->mutex};
            state->map = shared_map;
            state->stage = BootstrapStage::meshing;
            state->chunks_total = total;
        }

        // These products used to run synchronously on the START/class-selection
        // tick. Building them in parallel with chunk meshing keeps every
        // graphics backend responsive while the existing loading screen is
        // visible. The map is immutable throughout this phase.
        std::thread derived_worker{[state, shared_map, palette = map_palette]() {
            BootstrapDerivedWorld derived;
            derived.skylight.rebuild(*shared_map);
            derived.minimap_rgba = build_minimap_overview_rgba(*shared_map);
            derived.emissive.build(*shared_map, palette, StaticLightField{});
            derived.map_revision = shared_map->revision();
            if (state->cancelled.load()) {
                return;
            }
            const std::lock_guard lock{state->mutex};
            state->derived_world = std::move(derived);
        }};

        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (std::uint32_t worker{}; worker < workers; ++worker) {
            pool.emplace_back([state, shared_map, &mesher, chunks_per_axis, total]() {
                for (;;) {
                    const auto index = state->next_chunk.fetch_add(1U);
                    if (index >= total || state->cancelled.load()) {
                        return;
                    }
                    const ChunkKey key{static_cast<std::uint32_t>(index) % chunks_per_axis,
                                       static_cast<std::uint32_t>(index) / chunks_per_axis};
                    auto mesh = mesher.mesh(*shared_map, key);
                    std::unique_lock lock{state->mutex};
                    state->queue_space.wait(lock, [&state]() {
                        return state->ready.size() < mesh_queue_capacity || state->cancelled.load();
                    });
                    if (state->cancelled.load()) {
                        return;
                    }
                    state->ready.push_back(std::move(mesh));
                    ++state->chunks_meshed;
                }
            });
        }
        for (auto& worker : pool) {
            worker.join();
        }
        derived_worker.join();
        const std::lock_guard lock{state->mutex};
        if (!state->cancelled.load() && state->stage == BootstrapStage::meshing &&
            state->derived_world.has_value()) {
            state->stage = BootstrapStage::ready;
        }
    }};
}

void TutorialWorldBootstrap::cancel() noexcept {
    state_->cancelled.store(true);
    state_->queue_space.notify_all();
}

BootstrapProgress TutorialWorldBootstrap::progress() const {
    const std::lock_guard lock{state_->mutex};
    BootstrapProgress result;
    result.stage = state_->stage;
    result.chunks_meshed = state_->chunks_meshed;
    result.chunks_total = state_->chunks_total;
    result.error = state_->error;
    switch (state_->stage) {
    case BootstrapStage::idle:
        result.fraction = 0.0;
        break;
    case BootstrapStage::parsing:
        result.fraction = parse_fraction * 0.5;
        break;
    case BootstrapStage::meshing:
        result.fraction = parse_fraction + (state_->chunks_total == 0U
                                                ? 0.0
                                                : (1.0 - parse_fraction) *
                                                      static_cast<double>(state_->chunks_meshed) /
                                                      static_cast<double>(state_->chunks_total));
        break;
    case BootstrapStage::ready:
        result.fraction = 1.0;
        break;
    case BootstrapStage::failed:
        result.fraction = 0.0;
        break;
    }
    return result;
}

std::vector<ChunkMesh> TutorialWorldBootstrap::take_ready_meshes(std::size_t limit) {
    std::vector<ChunkMesh> taken;
    {
        const std::lock_guard lock{state_->mutex};
        const auto count = std::min(limit, state_->ready.size());
        taken.reserve(count);
        for (std::size_t index{}; index < count; ++index) {
            taken.push_back(std::move(state_->ready.front()));
            state_->ready.pop_front();
        }
    }
    if (!taken.empty()) {
        state_->queue_space.notify_all();
    }
    return taken;
}

std::shared_ptr<VxlMap> TutorialWorldBootstrap::map() const {
    const std::lock_guard lock{state_->mutex};
    return state_->map;
}

std::optional<BootstrapDerivedWorld> TutorialWorldBootstrap::take_derived_world() {
    const std::lock_guard lock{state_->mutex};
    auto result = std::move(state_->derived_world);
    state_->derived_world.reset();
    return result;
}

} // namespace battlespades::world
