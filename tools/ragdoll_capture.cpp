// Offscreen strobe capture of Classic corpses (ragdolls) on a synthetic field.
//
// One scenario is simulated to several times and the copies are drawn side by
// side, so a single image shows the fall from release to rest, with the same
// part meshes and transforms the frontend draws. For every copy it also prints
// how far each drawn part sits above (+) or inside (-) the ground, which is how
// a floating torso or a sunken limb shows up as a number.
//
// Usage: aos_ragdoll_capture out=<file.png> [scenario=stand|run|headshot|bodyshot|
//            backshot|wall|step|trench|drop|crouch] [class=5] [view=front|back|top]
//            [times=0,0.12,0.3,0.6,1.2,4] [yaw=-90] [seed=1] [spacing=4.5] [bounds=1]
//            [distance=11] [eye=2.4] [pitch=8] [pan=0] [fov=50] [width=1800] [height=600]
//            [assets=<root>] [shaders=<bin root>]

#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/quality_profile.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/class_models.hpp"
#include "battlespades/world/classic_corpse.hpp"
#include "battlespades/world/jetpack_death.hpp"
#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/map_atmosphere.hpp"
#include "battlespades/world/map_catalog.hpp"
#include "battlespades/world/retail_character_pose.hpp"
#include "battlespades/world/skylight_map.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades;
using Mat4 = std::array<float, 16U>;

void expect(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

constexpr std::uint32_t ground_z{60U};

struct Extent final {
    float lowest{std::numeric_limits<float>::lowest()};   // largest z: nearest the ground
    float highest{std::numeric_limits<float>::max()};     // smallest z
};

Extent world_extent(const world::ChunkMesh& mesh, const Mat4& m) {
    Extent extent;
    for (const auto& vertex : mesh.vertices) {
        const float z = m[2] * vertex.x + m[6] * vertex.y + m[10] * vertex.z + m[14];
        extent.lowest = std::max(extent.lowest, z);
        extent.highest = std::min(extent.highest, z);
    }
    return extent;
}

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
        expect(args.contains("out"), "out= is required");
        const auto scenario = get("scenario", "stand");
        const auto view = get("view", "front");
        const auto class_id = static_cast<std::uint8_t>(std::stoi(get("class", "5")));
        const auto width = static_cast<std::uint16_t>(std::stoi(get("width", "1800")));
        const auto height = static_cast<std::uint16_t>(std::stoi(get("height", "600")));
        const double yaw = std::stod(get("yaw", "-90"));
        const auto seed = static_cast<std::uint32_t>(std::stoul(get("seed", "1")));
        const double spacing = std::stod(get("spacing", "4.5"));
        std::vector<double> times;
        {
            std::stringstream stream{get("times", "0,0.12,0.3,0.6,1.2,4")};
            for (std::string token; std::getline(stream, token, ',');) times.push_back(std::stod(token));
        }
        expect(!times.empty(), "times= needs at least one value");

        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Ragdoll capture", width, height, SDL_WINDOW_HIDDEN);
        expect(window != nullptr, SDL_GetError());
        render::BgfxUiRenderer ui;
        render::BgfxUiRendererConfig config;
        config.native_window.window = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.asset_root = get("assets", AOS_TOOL_ASSET_ROOT);
        config.shader_root = get("shaders", AOS_TOOL_SHADER_BIN_ROOT);
        config.drawable_extent = {width, height};
        config.backend = render::GraphicsBackend::direct3d11;
        config.vertical_sync = false;
        config.multisample_samples = 0U;
        config.texture_quality = render::TextureQualityTier::medium;
        expect(ui.initialize(config), std::string{ui.last_error()});
        render::WorldRenderer scene;
        expect(scene.initialize(config.shader_root, config.asset_root, config.texture_quality),
               std::string{scene.last_error()});
        const auto color_target = bgfx::createTexture2D(width, height, false, 1,
                                                        bgfx::TextureFormat::RGBA8,
                                                        BGFX_TEXTURE_RT);
        const auto depth_target = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        const std::array attachments{color_target, depth_target};
        const auto framebuffer = bgfx::createFrameBuffer(2, attachments.data(), false);
        const auto readback = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);

        // Flat field: z 60 is the ground surface. Light checker so parts read.
        std::vector<std::byte> bytes;
        for (std::size_t column{}; column < static_cast<std::size_t>(world::VxlMap::width) *
                                                world::VxlMap::depth;
             ++column) {
            bytes.push_back(std::byte{0U});
            bytes.push_back(std::byte{1U});
            bytes.push_back(std::byte{0U});
            bytes.push_back(std::byte{0U});
        }
        auto loaded = world::VxlMap::load(bytes);
        expect(static_cast<bool>(loaded), "synthetic map");
        auto& map = *loaded.map;
        const auto block = [&](int x, int y, int z, world::VxlColor color) {
            static_cast<void>(map.set_voxel(static_cast<std::uint32_t>(x),
                                            static_cast<std::uint32_t>(y),
                                            static_cast<std::uint32_t>(z), color));
        };
        const auto clear = [&](int x, int y, int z) {
            static_cast<void>(map.clear_voxel(static_cast<std::uint32_t>(x),
                                              static_cast<std::uint32_t>(y),
                                              static_cast<std::uint32_t>(z)));
        };
        for (int y = 224; y < 290; ++y)
            for (int x = 200; x < 330; ++x)
                for (int z = static_cast<int>(ground_z); z < static_cast<int>(ground_z) + 4; ++z)
                    block(x, y, z,
                          world::VxlColor{static_cast<std::uint8_t>(150U + ((x + y) & 1) * 14),
                                          static_cast<std::uint8_t>(150U + ((x + y) & 1) * 14),
                                          static_cast<std::uint8_t>(140U + ((x + y) & 1) * 14), 255U});

        const double first_x = 262.0 - spacing * static_cast<double>(times.size() - 1U) * 0.5;
        const world::VxlColor obstacle{120U, 96U, 70U, 255U};
        // Obstacles are placed relative to each copy, in its direction of fall.
        const double yaw_radians = yaw * std::numbers::pi / 180.0;
        const world::Vec3 right{std::cos(yaw_radians), std::sin(yaw_radians), 0};
        const world::Vec3 forward{-right.y, right.x, 0};
        for (std::size_t index{}; index < times.size(); ++index) {
            const double cx = first_x + spacing * static_cast<double>(index), cy = 256.0;
            const auto at = [&](double along, double across) {
                return std::array<int, 2>{
                    static_cast<int>(std::floor(cx + forward.x * along + right.x * across)),
                    static_cast<int>(std::floor(cy + forward.y * along + right.y * across))};
            };
            if (scenario == "wall") {
                for (int across = -1; across <= 1; ++across)
                    for (int up = 1; up <= 3; ++up) {
                        const auto cell = at(1.4, across);
                        block(cell[0], cell[1], static_cast<int>(ground_z) - up, obstacle);
                    }
            } else if (scenario == "step") {
                for (int across = -1; across <= 1; ++across) {
                    const auto cell = at(1.2, across);
                    block(cell[0], cell[1], static_cast<int>(ground_z) - 1, obstacle);
                }
            } else if (scenario == "trench") {
                for (int across = -1; across <= 1; ++across)
                    for (int down = 0; down < 2; ++down) {
                        const auto cell = at(1.0, across);
                        clear(cell[0], cell[1], static_cast<int>(ground_z) + down);
                    }
            } else if (scenario == "drop") {
                // The body starts on a three-block ledge.
                for (int along = -1; along <= 0; ++along)
                    for (int across = -1; across <= 1; ++across)
                        for (int up = 1; up <= 3; ++up) {
                            const auto cell = at(along, across);
                            block(cell[0], cell[1], static_cast<int>(ground_z) - up, obstacle);
                        }
            }
        }

        const world::ChunkMesher mesher{world::ChunkMesherConfig{}};
        for (std::uint32_t y = 0; y < mesher.chunks_per_axis(); ++y)
            for (std::uint32_t x = 0; x < mesher.chunks_per_axis(); ++x)
                expect(scene.upload_chunk(mesher.mesh(map, {x, y})), "chunk upload");
        world::SkylightMap skylight;
        skylight.rebuild(map);
        scene.set_skylight_horizon(skylight.data(), world::SkylightMap::edge);
        const auto tier = settings::parse_shader_quality(get("tier", "compatibility"));
        expect(tier.has_value(), "unknown tier");
        scene.set_quality_profile(render::profile_for(*tier, settings::QualityLevel::medium));
        world::SkydomeConfidence confidence{};
        const auto dome = world::resolve_map_skydome(config.asset_root, "Crossroads", confidence);
        const std::array<std::uint8_t, 3U> fog{150U, 176U, 200U};
        expect(scene.set_skydome(dome), std::string{scene.last_error()});
        auto atmosphere = world::resolve_map_atmosphere(config.asset_root, dome);
        atmosphere.fog_color = fog;
        scene.set_atmosphere(atmosphere);
        scene.set_fog_color(fog);
        scene.set_retail_fog_color(fog);
        scene.set_retail_lighting(render::RetailTerrainLighting{});

        const auto asset_root = std::filesystem::path{config.asset_root};
        const auto color =
            world::retail_character_color(world::VxlColor{44U, 117U, 179U, 255U});
        auto models = world::load_class_models(asset_root, class_id, color);
        expect(static_cast<bool>(models), models.error);
        const auto& set = *models.models;
        // The player's own model: trunk, head, each leg cut at the knee, and the
        // class's two arm meshes drawn on both arms.
        using Corpse = world::ClassicCorpse;
        const std::vector<world::ChunkMesh> body{set.standing_torso_preview, set.head_preview,
                                                 set.leg_halves[0], set.leg_halves[1],
                                                 set.leg_halves[2], set.leg_halves[3]};
        std::uint32_t next_slot{96U};
        const auto upload = [&](const world::ChunkMesh& mesh) {
            const auto slot = next_slot++;
            expect(scene.set_world_model_mesh(slot, mesh), std::string{scene.last_error()});
            return slot;
        };
        std::vector<std::uint32_t> body_slots;
        for (const auto& mesh : body) body_slots.push_back(upload(mesh));
        std::array<std::uint32_t, 2U> arm_slots{};
        const bool arms = set.first_person_arms.size() >= 2U;
        if (arms)
            for (std::size_t part = 0; part < 2U; ++part)
                arm_slots[part] = upload(set.first_person_arms[part]);

        const char* const body_names[]{"torso", "head", "thigh_l", "shin_l", "thigh_r", "shin_r"};
        const char* const arm_names[]{"upper_l", "upper_r", "fore_l", "fore_r"};
        std::vector<render::WorldModelDraw> draws;
        std::cout << "scenario=" << scenario << " class=" << static_cast<int>(class_id)
                  << " yaw=" << yaw << "\n";
        if (get("bounds", "0") != "0") {
            const auto bounds = [](const char* name, const world::ChunkMesh& mesh) {
                std::cout << "  " << name << ": x " << mesh.minimum[0] << ".." << mesh.maximum[0]
                          << "  y " << mesh.minimum[1] << ".." << mesh.maximum[1] << "  z "
                          << mesh.minimum[2] << ".." << mesh.maximum[2] << "\n";
            };
            std::cout << "mesh bounds at rest:\n";
            for (std::size_t part = 0; part < body.size(); ++part)
                bounds(body_names[part], body[part]);
            if (arms) {
                bounds("arm_upper (own space)", set.first_person_arms[0]);
                bounds("arm_lower (own space)", set.first_person_arms[1]);
            }
        }
        double worst_sunk = 0, worst_float = 0;
        for (std::size_t index{}; index < times.size(); ++index) {
            const double cx = first_x + spacing * static_cast<double>(index);
            world::Vec3 position{cx, 256.0, static_cast<double>(ground_z) - 2.25};
            world::Vec3 velocity{};
            bool crouched = false;
            if (scenario == "run") velocity = {forward.x * 9.0, forward.y * 9.0, 0};
            // Walking into the obstacle, so the body meets it as it goes down.
            if (scenario == "wall" || scenario == "step" || scenario == "trench" ||
                scenario == "drop")
                velocity = {forward.x * 5.0, forward.y * 5.0, 0};
            // speed=/strafe= on any scenario: blocks a second forward and to the left.
            if (const auto speed = get("speed", ""); !speed.empty())
                velocity = {forward.x * std::stod(speed), forward.y * std::stod(speed), 0};
            if (const auto strafe = get("strafe", ""); !strafe.empty()) {
                velocity.x += right.x * std::stod(strafe);
                velocity.y += right.y * std::stod(strafe);
            }
            if (scenario == "crouch") {
                crouched = true;
                position.z = static_cast<double>(ground_z) - 1.35;
            }
            if (scenario == "drop") position.z -= 3.0;
            Corpse corpse{position, yaw, velocity, seed, true, crouched, 0};
            if (scenario == "headshot" || scenario == "bodyshot" || scenario == "backshot") {
                // The frontend's kill impulse: from the killer toward the struck joint.
                const auto target =
                    corpse.joint(scenario == "headshot" ? Corpse::head : Corpse::neck);
                const double sign = scenario == "backshot" ? 1.0 : -1.0;
                corpse.impulse(target, {sign * forward.x * 5.0, sign * forward.y * 5.0, -1.0});
            }
            // shot=front|back|left|right on any scenario, aim=head|neck: the kill
            // impulse as a killer standing on that side would deliver it.
            if (const auto shot = get("shot", ""); !shot.empty()) {
                const auto target =
                    corpse.joint(get("aim", "neck") == "head" ? Corpse::head : Corpse::neck);
                const world::Vec3 from = shot == "front"  ? world::Vec3{-forward.x, -forward.y, 0}
                                         : shot == "back" ? world::Vec3{forward.x, forward.y, 0}
                                         : shot == "left" ? world::Vec3{-right.x, -right.y, 0}
                                                          : world::Vec3{right.x, right.y, 0};
                corpse.impulse(target, {from.x * 5.0, from.y * 5.0, -1.0});
            }
            const double dt = 1.0 / 60.0;
            double peak_speed = 0;
            auto previous = corpse.joint(Corpse::pelvis);
            for (double t = 0; t + 1e-9 < times[index]; t += dt) {
                corpse.tick(dt, map, 1.0);
                const auto now = corpse.joint(Corpse::pelvis);
                peak_speed = std::max(peak_speed,
                                      std::sqrt((now.x - previous.x) * (now.x - previous.x) +
                                                (now.y - previous.y) * (now.y - previous.y) +
                                                (now.z - previous.z) * (now.z - previous.z)) / dt);
                previous = now;
            }
            std::cout << "t=" << times[index] << " resting=" << corpse.resting()
                      << " pelvis_peak_speed=" << peak_speed
                      << "\n  gap above ground (+ floats, - sunk):";
            const auto report = [&](const char* name, const world::ChunkMesh& mesh,
                                    const Mat4& transform) {
                const double gap = static_cast<float>(ground_z) - world_extent(mesh, transform).lowest;
                std::cout << " " << name << "=" << gap;
                // Only meaningful on the flat scenarios, once the body is down.
                worst_sunk = std::min(worst_sunk, gap);
                if (index + 1U == times.size()) worst_float = std::max(worst_float, gap);
            };
            const std::array<Mat4, 6U> transforms{
                corpse.body_transform(),    corpse.head_transform(),
                corpse.leg_transform(true), corpse.shin_transform(true),
                corpse.leg_transform(false), corpse.shin_transform(false)};
            for (std::size_t part = 0; part < transforms.size(); ++part) {
                draws.push_back({body_slots[part], transforms[part]});
                report(body_names[part], body[part], transforms[part]);
            }
            if (arms)
                for (std::size_t part = 0; part < 4U; ++part) {
                    const bool left = part % 2U == 0U, upper = part < 2U;
                    const auto& mesh = set.first_person_arms[upper ? 0U : 1U];
                    const auto transform = corpse.arm_transform(
                        left, upper, {mesh.minimum[0], mesh.minimum[1], mesh.minimum[2]},
                        {mesh.maximum[0], mesh.maximum[1], mesh.maximum[2]});
                    draws.push_back({arm_slots[upper ? 0U : 1U], transform});
                    report(arm_names[part], mesh, transform);
                }
            std::cout << "\n  joints (height above ground):";
            const char* const joint_names[]{"pelvis", "neck", "head", "sh_l", "sh_r", "el_l",
                                            "el_r", "hand_l", "hand_r", "foot_l", "foot_r",
                                            "hip_l", "hip_r", "knee_l", "knee_r"};
            for (std::size_t joint = 0; joint < Corpse::joint_count; ++joint)
                std::cout << " " << joint_names[joint] << "="
                          << static_cast<double>(ground_z) -
                                 corpse.joint(static_cast<Corpse::Joint>(joint)).z;
            std::cout << "\n";
        }
        std::cout << "deepest part below the ground at any time: " << -worst_sunk
                  << "; highest part bottom above it at the end: " << worst_float << "\n";

        render::WorldCamera camera;
        const double distance = std::stod(get("distance", "11"));
        // Positive pitch looks down. The eye height is in blocks above the ground.
        const double eye_height = std::stod(get("eye", view == "top" ? "0" : "2.4"));
        const double center_x = 262.0 + std::stod(get("pan", "0"));
        if (view == "top") {
            camera.eye = {center_x, 256.0 - 0.01, static_cast<double>(ground_z) - distance};
            camera.yaw_degrees = -90.0;
            camera.pitch_degrees = 89.0;
        } else if (view == "back") {
            camera.eye = {center_x, 256.0 + distance, static_cast<double>(ground_z) - eye_height};
            camera.yaw_degrees = 90.0;
            camera.pitch_degrees = std::stod(get("pitch", "8"));
        } else {
            camera.eye = {center_x, 256.0 - distance, static_cast<double>(ground_z) - eye_height};
            camera.yaw_degrees = -90.0;
            camera.pitch_degrees = std::stod(get("pitch", "8"));
        }
        camera.fov_y_degrees = std::stod(get("fov", "50"));
        camera.fog_distance = 192.0;

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
        for (int pass = 0; pass < 3; ++pass) {
            expect(ui.begin_frame(), std::string{ui.last_error()});
            expect(scene.submit(camera, config.drawable_extent, {}, draws),
                   std::string{scene.last_error()});
            for (const auto id : {render::backdrop_clear_view_id, render::world_view_id,
                                  render::view_model_view_id, render::ui_window_view_id,
                                  render::ui_canvas_view_id})
                bgfx::setViewFrameBuffer(id, framebuffer);
            bgfx::setViewFrameBuffer(render::ui_canvas_view_id + 1, BGFX_INVALID_HANDLE);
            bgfx::setViewRect(render::ui_canvas_view_id + 1, 0, 0, width, height);
            bgfx::touch(render::ui_canvas_view_id + 1);
            expect(ui.end_frame(), std::string{ui.last_error()});
            bgfx::blit(render::ui_canvas_view_id + 2, readback, 0, 0, color_target, 0, 0, width,
                       height);
            const auto ready = bgfx::readTexture(readback, pixels.data());
            bool done = false;
            for (unsigned frame = 0; frame < 32; ++frame) {
                if (bgfx::frame() >= ready) {
                    done = true;
                    break;
                }
            }
            expect(done, "GPU readback timed out");
        }
        if (bgfx::getCaps()->originBottomLeft) {
            const std::size_t stride = static_cast<std::size_t>(width) * 4U;
            for (std::size_t row = 0; row < height / 2U; ++row) {
                auto first = pixels.begin() + static_cast<std::ptrdiff_t>(row * stride);
                auto last =
                    pixels.begin() + static_cast<std::ptrdiff_t>((height - row - 1U) * stride);
                std::swap_ranges(first, first + static_cast<std::ptrdiff_t>(stride), last);
            }
        }
        for (std::size_t index = 3; index < pixels.size(); index += 4U) pixels[index] = 255U;
        const auto out = get("out", "");
        expect(stbi_write_png(out.c_str(), width, height, 4, pixels.data(), width * 4) != 0,
               "PNG write failed");
        std::cout << "wrote " << out << '\n';
        scene.shutdown();
        bgfx::destroy(readback);
        bgfx::destroy(framebuffer);
        bgfx::destroy(depth_target);
        bgfx::destroy(color_target);
        ui.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
