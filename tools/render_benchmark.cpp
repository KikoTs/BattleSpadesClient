// Headless production-renderer benchmark and pixel-identity probe.
//
// Drives WorldRenderer exactly as the frontend does (hidden window, offscreen
// target, vsync off) over a real map with synthetic players and particles, and
// reports frame-time percentiles, terrain mesh/upload cost and buffer memory.
// Nothing here is a second renderer.
//
// Usage (key=value, any order):
//   aos_render_benchmark map=<file.vxl> [frames=600] [warmup=60] [rounds=3]
//       [width=1920] [height=1080] [tier=compatibility|low|medium|high|ultra]
//       [fog_distance=192] [players=32] [particles=1500] [skydome=none|auto]
//       [tuning=ab|legacy|tuned] [capture=<dir>] [captures=4] [label=<name>]
//       [extras=1|0] [sleep_test=0|1] [backend=direct3d11] [shaders=<bin root>]
//       [pacing_refresh=144] [pacing_tick_us=1500] [pacing_frame_us=800]
//       [pacing_seconds=5]
//
// `extras=0` draws only terrain, players and particles (the scene the first
// baseline captures used). `sleep_test=1` also runs the frame pacing probe.
//
// `tuning=ab` (the default when the build has render tuning) interleaves the
// legacy and tuned render paths in one process so both see the same machine
// load, and compares their captures byte for byte.

#include "battlespades/core/application.hpp"
#include "battlespades/core/frame_pacing.hpp"
#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/quality_profile.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/map_atmosphere.hpp"
#include "battlespades/world/map_catalog.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/skylight_map.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#if __has_include("battlespades/render/render_tuning.hpp")
#include "battlespades/render/render_tuning.hpp"
#define AOS_BENCH_HAS_TUNING 1
#else
#define AOS_BENCH_HAS_TUNING 0
#endif

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace battlespades;
using Clock = std::chrono::steady_clock;

void expect(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

struct Distribution final {
    double average{};
    double median{};
    double p99{};
    double p999{};
    double worst{};
};

[[nodiscard]] Distribution summarize(std::vector<double> samples) {
    Distribution result;
    if (samples.empty()) {
        return result;
    }
    std::ranges::sort(samples);
    double total{};
    for (const double sample : samples) {
        total += sample;
    }
    const auto at = [&](double fraction) {
        const auto index = static_cast<std::size_t>(
            std::min(static_cast<double>(samples.size() - 1U),
                     std::floor(fraction * static_cast<double>(samples.size()))));
        return samples[index];
    };
    result.average = total / static_cast<double>(samples.size());
    result.median = at(0.5);
    result.p99 = at(0.99);
    result.p999 = at(0.999);
    result.worst = samples.back();
    return result;
}

void print(const std::string& label, const Distribution& value) {
    std::cout << std::fixed << std::setprecision(3) << "  " << std::left << std::setw(30)
              << label << " avg " << std::setw(8) << value.average << " p50 " << std::setw(8)
              << value.median << " p99 " << std::setw(8) << value.p99 << " p99.9 "
              << std::setw(8) << value.p999 << " worst " << value.worst << '\n';
}

/** First solid voxel from the sky down, or the bed. */
[[nodiscard]] double ground_height(const world::VxlMap& map, double x, double y) {
    const auto cell_x = static_cast<std::uint32_t>(
        std::clamp(x, 0.0, static_cast<double>(world::VxlMap::width) - 1.0));
    const auto cell_y = static_cast<std::uint32_t>(
        std::clamp(y, 0.0, static_cast<double>(world::VxlMap::depth) - 1.0));
    for (std::uint32_t z{}; z < world::VxlMap::height; ++z) {
        if (map.solid(cell_x, cell_y, z)) {
            return static_cast<double>(z);
        }
    }
    return static_cast<double>(world::VxlMap::height) - 1.0;
}

[[nodiscard]] std::array<float, 16U> translation(float x, float y, float z, float yaw_degrees,
                                                 float scale) {
    const float radians = yaw_degrees * std::numbers::pi_v<float> / 180.0F;
    const float c = std::cos(radians) * scale;
    const float s = std::sin(radians) * scale;
    return {c,    s,    0.0F,  0.0F, -s,   c,   0.0F, 0.0F,
            0.0F, 0.0F, scale, 0.0F, x,    y,   z,    1.0F};
}

struct Scene final {
    render::WorldCamera camera;
    std::vector<render::WorldModelDraw> models;
    // One of everything else the world pass draws, so the legacy/tuned
    // comparison covers the view-model overrides and the blended passes too.
    std::vector<render::ViewModelDraw> tools;
    std::vector<world::DynamicLight> lights;
    std::vector<render::LaserBeamDraw> lasers;
    std::vector<render::SpotShadowDraw> shadows;
    std::vector<render::ZoneVolumeDraw> zones;
};

struct RunStats final {
    std::vector<double> submit;
    std::vector<double> present;
    std::vector<double> total;
    /** bgfx's GPU timer query over the whole frame, when the backend has one. */
    std::vector<double> gpu;
    std::vector<double> upload;
    std::uint64_t vertex_bytes{};
    std::uint64_t index_bytes{};
    std::int64_t gpu_memory_used{};
    std::size_t chunks_submitted{};
    std::uint32_t draw_calls{};
};

#if AOS_BENCH_HAS_TUNING
void burn(std::chrono::nanoseconds duration) {
    const auto until = Clock::now() + duration;
    while (Clock::now() < until) {
    }
}

/** One presented frame: when it reached the screen and the fraction it drew. */
struct PacedFrame final {
    Clock::time_point presented{};
    /** Wall time the drawn state stands for, on the tick time line. */
    Clock::time_point drawn_for{};
};

struct PacingProbeConfig final {
    std::chrono::nanoseconds period{};
    /** Simulation and network work of a tick, before its frame is drawn. */
    std::chrono::nanoseconds tick_work{};
    /** Building and presenting one frame. */
    std::chrono::nanoseconds frame_work{};
    std::uint64_t ticks{};
};

/**
 * The frame loop as it was before this change, restated: tick frame drawn at
 * alpha zero, render-only frames a display period apart after it, plain sleeps.
 */
[[nodiscard]] std::vector<PacedFrame> run_previous_pacing(const PacingProbeConfig& probe) {
    constexpr std::chrono::nanoseconds fixed_delta{16'666'666};
    std::vector<PacedFrame> frames;
    core::FixedStepPacer pacer{Clock::now(), fixed_delta};
    for (std::uint64_t tick{}; tick < probe.ticks; ++tick) {
        const auto pacing = pacer.step(Clock::now());
        const auto tick_time = pacing.next_tick - fixed_delta;
        burn(probe.tick_work);
        if (pacing.present) {
            burn(probe.frame_work);
            frames.push_back({Clock::now(), tick_time});
            auto previous_frame = Clock::now();
            while (true) {
                const auto slot = core::next_intermediate_frame(previous_frame, Clock::now(),
                                                                pacing.next_tick, probe.period);
                if (!slot.has_value()) {
                    break;
                }
                std::this_thread::sleep_until(slot->at);
                const auto woke = Clock::now();
                if (woke + probe.period / 4 >= pacing.next_tick) {
                    break;
                }
                const double alpha = core::intermediate_frame_alpha(tick_time, woke, fixed_delta);
                burn(probe.frame_work);
                frames.push_back(
                    {Clock::now(),
                     tick_time + std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::duration<double>(fixed_delta) * alpha)});
                previous_frame = slot->at;
            }
        }
        std::this_thread::sleep_until(pacing.next_tick);
    }
    return frames;
}

/** The shipped Application loop, observed through a module. */
class PacingProbe final : public core::RuntimeModule {
public:
    PacingProbe(const PacingProbeConfig& probe, std::vector<PacedFrame>& frames) noexcept
        : probe_{probe}, frames_{frames} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "pacing-probe"; }
    [[nodiscard]] bool start() override { return true; }
    void stop() noexcept override {}

    [[nodiscard]] core::TickDecision tick(const core::TickContext& context) override {
        tick_time_ = context.scheduled_at;
        fixed_delta_ = context.fixed_delta;
        burn(probe_.tick_work);
        if (context.present) {
            // As the frontend does: the tick's frame is drawn at the fraction
            // of the step that has passed when it is built.
            const double alpha =
                core::intermediate_frame_alpha(tick_time_, Clock::now(), fixed_delta_);
            burn(probe_.frame_work);
            record(alpha);
        }
        return core::TickDecision::continue_running;
    }

    [[nodiscard]] std::chrono::nanoseconds intermediate_frame_period() const noexcept override {
        return probe_.period;
    }

    [[nodiscard]] core::TickDecision present_intermediate(double alpha) override {
        burn(probe_.frame_work);
        record(alpha);
        return core::TickDecision::continue_running;
    }

private:
    void record(double alpha) {
        frames_.push_back(
            {Clock::now(),
             tick_time_ + std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::duration<double>(fixed_delta_) * alpha)});
    }

    PacingProbeConfig probe_;
    std::vector<PacedFrame>& frames_;
    Clock::time_point tick_time_{};
    std::chrono::nanoseconds fixed_delta_{};
};

void report_pacing(const std::string& name, const std::vector<PacedFrame>& frames,
                   double seconds) {
    std::vector<double> intervals;
    std::vector<double> motion;
    for (std::size_t index = 1U; index < frames.size(); ++index) {
        const double shown = milliseconds(frames[index].presented - frames[index - 1U].presented);
        const double moved = milliseconds(frames[index].drawn_for - frames[index - 1U].drawn_for);
        intervals.push_back(shown);
        // What the eye sees as stutter: how far the world moved in a frame
        // against how long that frame took to arrive.
        motion.push_back(std::fabs(moved - shown));
    }
    double mean{};
    for (const double value : intervals) {
        mean += value;
    }
    mean = intervals.empty() ? 0.0 : mean / static_cast<double>(intervals.size());
    double variance{};
    for (const double value : intervals) {
        variance += (value - mean) * (value - mean);
    }
    const double deviation =
        intervals.empty() ? 0.0 : std::sqrt(variance / static_cast<double>(intervals.size()));
    std::cout << name << ": " << frames.size() << " frames in " << seconds << " s ("
              << std::fixed << std::setprecision(1)
              << static_cast<double>(frames.size()) / seconds
              << " fps), frame interval deviation " << std::setprecision(3) << deviation
              << " ms\n";
    print("frame interval ms", summarize(intervals));
    print("motion error per frame ms", summarize(motion));
}
#endif

} // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        for (int index = 1; index < argc; ++index) {
            const std::string argument{argv[index]};
            const auto split = argument.find('=');
            expect(split != std::string::npos, "arguments are key=value: " + argument);
            args[argument.substr(0, split)] = argument.substr(split + 1U);
        }
        const auto get = [&](const std::string& key, const std::string& fallback) {
            const auto found = args.find(key);
            return found == args.end() ? fallback : found->second;
        };

        const bool sleep_test = get("sleep_test", "0") == "1";
        expect(sleep_test || args.contains("map"), "map= is required");
        const auto width = static_cast<std::uint16_t>(std::stoi(get("width", "1920")));
        const auto height = static_cast<std::uint16_t>(std::stoi(get("height", "1080")));
        const auto frames = static_cast<std::size_t>(std::stoul(get("frames", "600")));
        const auto warmup = static_cast<std::size_t>(std::stoul(get("warmup", "60")));
        const auto rounds = static_cast<std::size_t>(std::stoul(get("rounds", "3")));
        const auto player_count = static_cast<std::uint32_t>(std::stoul(get("players", "32")));
        const auto particle_count =
            static_cast<std::uint32_t>(std::stoul(get("particles", "1500")));
        const auto capture_count = static_cast<std::size_t>(std::stoul(get("captures", "4")));
        const auto capture_root = get("capture", "");
        const auto label = get("label", "bench");
        const double fog_distance = std::stod(get("fog_distance", "192"));
        expect(frames > 0U && rounds > 0U, "frames and rounds must be positive");

        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Render benchmark", width, height, SDL_WINDOW_HIDDEN);
        expect(window != nullptr, SDL_GetError());
        render::BgfxUiRenderer ui;
        render::BgfxUiRendererConfig config;
        config.native_window.window = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.asset_root = get("assets", AOS_TOOL_ASSET_ROOT);
        config.shader_root = get("shaders", AOS_TOOL_SHADER_BIN_ROOT);
        config.drawable_extent = {width, height};
        const auto backend = get("backend", "direct3d11");
        config.backend = backend == "direct3d12" ? render::GraphicsBackend::direct3d12
                         : backend == "vulkan"   ? render::GraphicsBackend::vulkan
                         : backend == "opengl"   ? render::GraphicsBackend::opengl
                                                 : render::GraphicsBackend::direct3d11;
        config.vertical_sync = false;
        config.multisample_samples = 0U;
        config.texture_quality = render::TextureQualityTier::medium;
        expect(ui.initialize(config), std::string{ui.last_error()});

        render::WorldRenderer scene;
        expect(scene.initialize(config.shader_root, config.asset_root, config.texture_quality),
               std::string{scene.last_error()});
        if (sleep_test) {
            // How late a short wait wakes once SDL and the renderer are up, as
            // in the running client. `std` is the pacer's previous sleep.
            const auto measure = [](const std::string& name, int wait_us, auto&& sleep) {
                std::vector<double> late;
                for (int repeat = 0; repeat < 200; ++repeat) {
                    const auto target = Clock::now() + std::chrono::microseconds{wait_us};
                    sleep(target);
                    late.push_back(milliseconds(Clock::now() - target));
                }
                print(name + " " + std::to_string(wait_us) + "us late ms", summarize(late));
            };
            for (const int wait_us : {500, 1000, 2000, 4000, 6944}) {
                measure("std", wait_us,
                        [](Clock::time_point target) { std::this_thread::sleep_until(target); });
#if AOS_BENCH_HAS_TUNING
                measure("precise", wait_us,
                        [](Clock::time_point target) { core::precise_sleep_until(target); });
#endif
            }
#if AOS_BENCH_HAS_TUNING
            // Frame pacing with synthetic work, previous loop against the
            // shipped one: pacing_refresh= (Hz), pacing_tick_us=,
            // pacing_frame_us=, pacing_seconds=.
            PacingProbeConfig probe;
            const double refresh = std::stod(get("pacing_refresh", "144"));
            const double seconds = std::stod(get("pacing_seconds", "5"));
            probe.period = std::chrono::nanoseconds{
                static_cast<std::int64_t>(1'000'000'000.0 / refresh)};
            probe.tick_work = std::chrono::microseconds{std::stoll(get("pacing_tick_us", "1500"))};
            probe.frame_work =
                std::chrono::microseconds{std::stoll(get("pacing_frame_us", "800"))};
            probe.ticks = static_cast<std::uint64_t>(seconds * 60.0);
            std::cout << "pacing at " << refresh << " Hz, tick work "
                      << milliseconds(probe.tick_work) << " ms, frame work "
                      << milliseconds(probe.frame_work) << " ms\n";
            report_pacing("previous loop", run_previous_pacing(probe), seconds);
            std::vector<PacedFrame> paced;
            core::RuntimeConfig runtime;
            runtime.tick_limit = probe.ticks;
            runtime.pace_to_wall_clock = true;
            core::Application application{runtime};
            expect(application.add_module(std::make_unique<PacingProbe>(probe, paced)),
                   "pacing probe must register");
            expect(application.run() == core::RunResult::success, "paced run failed");
            report_pacing("shipped loop", paced, seconds);
#endif
            if (!args.contains("map")) {
                scene.shutdown();
                ui.shutdown();
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }
        }

        const auto color = bgfx::createTexture2D(width, height, false, 1,
                                                 bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        const auto depth = bgfx::createTexture2D(width, height, false, 1,
                                                 bgfx::TextureFormat::D24S8,
                                                 BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        const std::array attachments{color, depth};
        const auto framebuffer = bgfx::createFrameBuffer(2, attachments.data(), false);
        const auto readback = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        expect(bgfx::isValid(framebuffer) && bgfx::isValid(readback), "capture target failed");

        auto map = world::VxlMap::load_file(get("map", ""));
        expect(static_cast<bool>(map), "map load failed");
        const auto map_name = std::filesystem::path{get("map", "")}.stem().string();

        const auto tier_name = get("tier", "compatibility");
        const auto tier = settings::parse_shader_quality(tier_name);
        expect(tier.has_value(), "unknown tier " + tier_name);
        scene.set_quality_profile(render::profile_for(*tier, settings::QualityLevel::medium));

        world::ChunkMesherConfig mesher_config;
        if (tier_name != "compatibility") {
            mesher_config.emissive = world::emissive_palette_for(map_name);
        }
        const world::ChunkMesher mesher{mesher_config};
        const auto per_axis = mesher.chunks_per_axis();

        // ---- Mesh every chunk once, timing each (the live remesh spike). ----
        std::vector<world::ChunkMesh> meshes;
        meshes.reserve(static_cast<std::size_t>(per_axis) * per_axis);
        std::vector<double> mesh_times;
        std::uint64_t vertex_count{};
        std::uint64_t index_count{};
        for (std::uint32_t y = 0; y < per_axis; ++y) {
            for (std::uint32_t x = 0; x < per_axis; ++x) {
                const auto begin = Clock::now();
                auto mesh = mesher.mesh(*map.map, {x, y});
                mesh_times.push_back(milliseconds(Clock::now() - begin));
                vertex_count += mesh.vertices.size();
                index_count += mesh.indices.size();
                meshes.push_back(std::move(mesh));
            }
        }
        std::cout << "map " << map_name << " tier " << tier_name << " " << width << 'x' << height
                  << " fog " << fog_distance << " players " << player_count << " particles "
                  << particle_count << '\n';
        std::cout << "terrain: " << vertex_count << " vertices, " << index_count << " indices\n";
        print("chunk mesh ms", summarize(mesh_times));

        world::SkylightMap skylight;
        skylight.rebuild(*map.map);
        scene.set_skylight_horizon(skylight.data(), world::SkylightMap::edge);
        scene.set_retail_sea_color(
            render::retail_sea_color_for(*map.map, mesher_config.bed_water_color));
        const std::array<std::uint8_t, 3U> fog_bytes{128U, 160U, 200U};
        scene.set_fog_color(fog_bytes);
        scene.set_retail_fog_color(fog_bytes);
        if (get("skydome", "none") == "auto") {
            world::SkydomeConfidence confidence{};
            const auto dome =
                world::resolve_map_skydome(config.asset_root, map_name, confidence);
            expect(scene.set_skydome(dome), std::string{scene.last_error()});
            auto atmosphere = world::resolve_map_atmosphere(config.asset_root, dome);
            atmosphere.fog_color = fog_bytes;
            scene.set_atmosphere(atmosphere);
            scene.set_fog_color(fog_bytes);
            scene.set_retail_fog_color(fog_bytes);
        }

        // ---- Synthetic players: twelve cube parts each, in player slots. ----
        constexpr std::uint32_t parts_per_player{12U};
        constexpr std::uint32_t player_slot_base{96U};
        const auto cube = world::placement_preview_cube({200U, 60U, 60U, 255U});
        for (std::uint32_t player{}; player < player_count; ++player) {
            for (std::uint32_t part{}; part < parts_per_player; ++part) {
                expect(scene.set_world_model_mesh(
                           player_slot_base + player * parts_per_player + part, cube),
                       std::string{scene.last_error()});
            }
        }

        expect(scene.set_view_model_mesh(0U, cube) && scene.set_view_model_mesh(1U, cube),
               std::string{scene.last_error()});
        const bool extras = get("extras", "1") == "1";

        const auto scene_at = [&](std::size_t frame) {
            Scene result;
            const double turn = 2.0 * std::numbers::pi * static_cast<double>(frame) /
                                static_cast<double>(frames);
            const double centre_x = static_cast<double>(world::VxlMap::width) * 0.5;
            const double centre_y = static_cast<double>(world::VxlMap::depth) * 0.5;
            const double eye_x = centre_x + std::cos(turn) * 120.0;
            const double eye_y = centre_y + std::sin(turn) * 120.0;
            result.camera.eye = {eye_x, eye_y, ground_height(*map.map, eye_x, eye_y) - 3.0};
            // Look across the map centre, sweeping slowly.
            result.camera.yaw_degrees =
                turn * 180.0 / std::numbers::pi + 20.0 * std::sin(turn * 3.0);
            result.camera.pitch_degrees = 6.0;
            result.camera.fog_distance = fog_distance;
            result.models.reserve(static_cast<std::size_t>(player_count) * parts_per_player);
            for (std::uint32_t player{}; player < player_count; ++player) {
                const double around = turn * 2.0 + static_cast<double>(player) * 0.61;
                const double radius = 12.0 + static_cast<double>(player % 8U) * 9.0;
                const double px = eye_x - std::cos(turn) * 40.0 + std::cos(around) * radius;
                const double py = eye_y - std::sin(turn) * 40.0 + std::sin(around) * radius;
                const double pz = ground_height(*map.map, px, py) - 2.5;
                for (std::uint32_t part{}; part < parts_per_player; ++part) {
                    render::WorldModelDraw draw;
                    draw.slot = player_slot_base + player * parts_per_player + part;
                    draw.transform = translation(
                        static_cast<float>(px),
                        static_cast<float>(py),
                        static_cast<float>(pz) + static_cast<float>(part) * 0.2F,
                        static_cast<float>(around * 57.0),
                        0.5F);
                    result.models.push_back(draw);
                }
            }
            if (extras) {
                const float ex = static_cast<float>(eye_x);
                const float ey = static_cast<float>(eye_y);
                const float ez = static_cast<float>(result.camera.eye[2U]);
                const float cx = static_cast<float>(centre_x);
                const float cy = static_cast<float>(centre_y);
                // Two view-space parts in front of the eye, one of them unlit.
                render::ViewModelDraw held;
                held.slot = 0U;
                held.transform = translation(0.35F, -0.30F, -1.2F, 25.0F, 0.25F);
                result.tools.push_back(held);
                held.slot = 1U;
                held.unlit = true;
                held.transform = translation(-0.30F, -0.25F, -1.4F, -40.0F, 0.2F);
                result.tools.push_back(held);
                // A translucent ghost takes the two-pass blended model path.
                render::WorldModelDraw ghost;
                ghost.slot = player_slot_base;
                ghost.opacity = 0.5F;
                ghost.transform = translation(ex + (cx - ex) * 0.1F, ey + (cy - ey) * 0.1F,
                                              ez + 1.0F, 30.0F, 1.0F);
                result.models.push_back(ghost);
                const float ground =
                    static_cast<float>(ground_height(*map.map, eye_x, eye_y));
                result.shadows.push_back({{ex + (cx - ex) * 0.05F, ey + (cy - ey) * 0.05F,
                                           ground - 0.01F}, 1.0F, 0.72F});
                result.shadows.push_back({{ex + 3.0F, ey + 2.0F, ground - 0.01F}, 1.0F, 0.4F});
                result.zones.push_back({{cx - 12.0F, cy - 12.0F, ground - 14.0F},
                                        {cx + 12.0F, cy + 12.0F, ground + 2.0F},
                                        {0.2F, 0.5F, 1.0F}, 0.3F, false});
                result.zones.push_back({{ex - 30.0F, ey - 6.0F, ground - 8.0F},
                                        {ex - 18.0F, ey + 6.0F, ground + 1.0F},
                                        {1.0F, 0.3F, 0.2F}, 0.45F, true});
                render::LaserBeamDraw beam;
                beam.pose.visible = true;
                beam.pose.color = world::SniperLaserColor::blue;
                beam.pose.origin = {cx, cy, ground - 6.0F};
                const float length = std::sqrt((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy) +
                                               36.0F);
                beam.pose.direction = {(ex - cx) / length, (ey - cy) / length, 0.0F};
                beam.pose.distance = length * 0.6F;
                beam.pose.alpha = 0.8F;
                beam.pose.player_hit = true;
                result.lasers.push_back(beam);
                result.lights.push_back({{ex + (cx - ex) * 0.2F, ey + (cy - ey) * 0.2F,
                                          ground - 3.0F}, {1.0F, 0.7F, 0.3F}, 12.0F, 1.5F});
            }
            return result;
        };

        const auto redirect_views = [&] {
            for (const auto id : {render::backdrop_clear_view_id, render::world_view_id,
                                  render::view_model_view_id, render::ui_window_view_id,
                                  render::ui_canvas_view_id}) {
                bgfx::setViewFrameBuffer(id, framebuffer);
            }
            bgfx::setViewFrameBuffer(render::ui_canvas_view_id + 1, BGFX_INVALID_HANDLE);
            bgfx::setViewRect(render::ui_canvas_view_id + 1, 0, 0, width, height);
            bgfx::touch(render::ui_canvas_view_id + 1);
        };

        std::vector<std::size_t> capture_frames;
        if (!capture_root.empty() && capture_count > 0U) {
            std::filesystem::create_directories(capture_root);
            for (std::size_t index{}; index < capture_count; ++index) {
                capture_frames.push_back(index * frames / capture_count);
            }
        }

        const auto upload_all = [&](RunStats& stats) {
            scene.clear_chunks();
            for (const auto& mesh : meshes) {
                const auto begin = Clock::now();
                expect(scene.upload_chunk(mesh), std::string{scene.last_error()});
                stats.upload.push_back(milliseconds(Clock::now() - begin));
            }
#if AOS_BENCH_HAS_TUNING
            const auto memory = scene.terrain_memory();
            stats.vertex_bytes = memory.vertex_bytes;
            stats.index_bytes = memory.index_bytes;
#else
            stats.vertex_bytes = vertex_count * sizeof(world::ChunkVertex);
            stats.index_bytes = index_count * sizeof(std::uint32_t);
#endif
        };

        using Pixels = std::vector<std::uint8_t>;
        const auto run = [&](RunStats& stats, const std::string& run_label,
                             std::vector<Pixels>* captures) {
            world::ParticleSystem particles;
            world::ParticleSpawn spawn;
            spawn.lifetime = 1.0e6F;
            spawn.gravity_scale = 0.0F;
            spawn.collide = false;
            spawn.explode_velocity = 0.02F;
            for (std::size_t frame{}; frame < warmup + frames; ++frame) {
                const bool measured = frame >= warmup;
                const std::size_t index = measured ? frame - warmup : 0U;
                const auto content = scene_at(index);
                if (frame == 0U) {
                    spawn.position = {static_cast<float>(content.camera.eye[0U]) - 20.0F,
                                      static_cast<float>(content.camera.eye[1U]),
                                      static_cast<float>(content.camera.eye[2U]) - 4.0F};
                    particles.emit_burst(spawn, particle_count, 0x5EEDU);
                }
                if (measured) {
                    particles.tick_unbounded(1.0 / 60.0);
                }
                particles.build_draw_list({static_cast<float>(content.camera.eye[0U]),
                                           static_cast<float>(content.camera.eye[1U]),
                                           static_cast<float>(content.camera.eye[2U])},
                                          static_cast<float>(fog_distance));
                const auto begin = Clock::now();
                expect(ui.begin_frame(), std::string{ui.last_error()});
                expect(scene.submit(content.camera, config.drawable_extent, content.tools,
                                    content.models, particles.instances(),
                                    particles.batches(), content.lights, content.lasers,
                                    content.shadows, content.zones),
                       std::string{scene.last_error()});
                const auto submitted = Clock::now();
                redirect_views();
                expect(ui.end_frame(), std::string{ui.last_error()});
                const auto presented = Clock::now();
                if (measured) {
                    stats.submit.push_back(milliseconds(submitted - begin));
                    stats.present.push_back(milliseconds(presented - submitted));
                    stats.total.push_back(milliseconds(presented - begin));
                    stats.chunks_submitted = scene.last_frame_stats().chunks_submitted;
                    const auto* bgfx_stats = bgfx::getStats();
                    stats.draw_calls = bgfx_stats->numDraw;
                    if (bgfx_stats->gpuTimerFreq > 0 &&
                        bgfx_stats->gpuTimeEnd > bgfx_stats->gpuTimeBegin) {
                        stats.gpu.push_back(
                            static_cast<double>(bgfx_stats->gpuTimeEnd -
                                                bgfx_stats->gpuTimeBegin) *
                            1000.0 / static_cast<double>(bgfx_stats->gpuTimerFreq));
                    }
                    stats.gpu_memory_used = bgfx_stats->gpuMemoryUsed;
                }
                if (measured && captures != nullptr &&
                    std::ranges::find(capture_frames, index) != capture_frames.end()) {
                    Pixels pixels(static_cast<std::size_t>(width) * height * 4U);
                    bgfx::blit(render::ui_canvas_view_id + 2, readback, 0, 0, color, 0, 0, width,
                               height);
                    const auto ready = bgfx::readTexture(readback, pixels.data());
                    bool done = false;
                    for (unsigned wait = 0; wait < 32; ++wait) {
                        if (bgfx::frame() >= ready) {
                            done = true;
                            break;
                        }
                    }
                    expect(done, "GPU readback timed out");
                    for (std::size_t alpha = 3; alpha < pixels.size(); alpha += 4U) {
                        pixels[alpha] = 255U;
                    }
                    if (!capture_root.empty()) {
                        const auto path = std::filesystem::path{capture_root} /
                                          (label + "_" + run_label + "_f" +
                                           std::to_string(index) + ".png");
                        expect(stbi_write_png(path.string().c_str(), width, height, 4,
                                              pixels.data(), width * 4) != 0,
                               "PNG write failed");
                    }
                    captures->push_back(std::move(pixels));
                }
            }
        };

        const auto report = [&](const std::string& name, const RunStats& stats) {
            std::cout << name << ": chunks submitted " << stats.chunks_submitted
                      << ", bgfx draws " << stats.draw_calls << ", terrain vertex bytes "
                      << stats.vertex_bytes << ", index bytes " << stats.index_bytes
                      << ", gpu memory used " << stats.gpu_memory_used << '\n';
            print("submit ms", summarize(stats.submit));
            print("bgfx frame ms", summarize(stats.present));
            print("frame total ms", summarize(stats.total));
            print("gpu frame ms", summarize(stats.gpu));
            print("chunk upload ms", summarize(stats.upload));
        };

#if AOS_BENCH_HAS_TUNING
        const auto tuning_mode = get("tuning", "ab");
        const auto legacy = render::RenderTuning::legacy();
        const render::RenderTuning tuned{};
        RunStats legacy_stats;
        RunStats tuned_stats;
        std::vector<Pixels> legacy_pixels;
        std::vector<Pixels> tuned_pixels;
        for (std::size_t round{}; round < rounds; ++round) {
            const bool first = round == 0U;
            if (tuning_mode != "tuned") {
                scene.set_render_tuning(legacy);
                upload_all(legacy_stats);
                run(legacy_stats, "legacy", first ? &legacy_pixels : nullptr);
            }
            if (tuning_mode != "legacy") {
                scene.set_render_tuning(tuned);
                upload_all(tuned_stats);
                run(tuned_stats, "tuned", first ? &tuned_pixels : nullptr);
            }
        }
        if (tuning_mode != "tuned") {
            report("legacy", legacy_stats);
        }
        if (tuning_mode != "legacy") {
            report("tuned", tuned_stats);
        }
        int exit_code = 0;
        if (tuning_mode == "ab" && !legacy_pixels.empty()) {
            std::size_t differing{};
            int largest{};
            for (std::size_t image{}; image < legacy_pixels.size(); ++image) {
                const auto& a = legacy_pixels[image];
                const auto& b = tuned_pixels.at(image);
                for (std::size_t byte{}; byte < a.size(); ++byte) {
                    const int delta = std::abs(static_cast<int>(a[byte]) -
                                               static_cast<int>(b[byte]));
                    if (delta != 0) {
                        ++differing;
                        largest = std::max(largest, delta);
                    }
                }
            }
            std::cout << "pixel identity legacy vs tuned over " << legacy_pixels.size()
                      << " captures: " << differing << " differing bytes, largest delta "
                      << largest << (differing == 0U ? " -- IDENTICAL" : " -- DIFFERENT")
                      << '\n';
            if (differing != 0U) {
                exit_code = 2;
            }
        }
#else
        RunStats stats;
        std::vector<Pixels> pixels;
        for (std::size_t round{}; round < rounds; ++round) {
            upload_all(stats);
            run(stats, "baseline", round == 0U ? &pixels : nullptr);
        }
        report("baseline", stats);
        const int exit_code = 0;
#endif

        scene.shutdown();
        bgfx::destroy(readback);
        bgfx::destroy(framebuffer);
        bgfx::destroy(depth);
        bgfx::destroy(color);
        ui.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return exit_code;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
