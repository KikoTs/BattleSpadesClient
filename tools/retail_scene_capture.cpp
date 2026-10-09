// Offscreen production-renderer capture of a real map at an exact camera pose.
//
// Used to compare the RETAIL (compatibility) tier against screenshots of the
// retail client taken at the same eye/orientation/FOV/lighting/fog. Nothing
// here is a second renderer: it drives WorldRenderer exactly as the frontend
// does, with the StateData lighting and fog passed on the command line.
//
// Usage (key=value, any order):
//   aos_retail_scene_capture map=<file.vxl> out=<file.png>
//       eye=x,y,z ori=x,y,z [skydome=Name.txt] [width=800] [height=600]
//       [fov=75] [fog_distance=192] [fog=r,g,b]
//       [light=r,g,b] [light_dir=x,y,z] [back=r,g,b] [back_dir=x,y,z]
//       [ambient=r,g,b] [ambient_intensity=f] [tier=compatibility|low|...]
//       [backend=direct3d11] [shaders=<bin root>] [sky=x,y,z]
//       [classic075=1] (original horizontal fog, clamped to 128 blocks)
//       [explosion_tool=57 effect_position=x,y,z effect_ticks=12]
//       [effect_color=r,g,b] [effect_gravity=1] [effect_collision=1]
//       [sticky_fragments=1] (include the attached model breakup for tool 57)
// Colours are 0..255 bytes; directions are StateData (retail GL basis) values.
// sky= centres the skydome on a map position instead of the eye (the retail
// menu backdrop before create_player).
// The optional explosion fixture runs the production particle emitter at an
// exact fixed-tick age, independent of frame timing, network and user input.

#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/quality_profile.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/map_atmosphere.hpp"
#include "battlespades/world/map_catalog.hpp"
#include "battlespades/world/particle_effects.hpp"
#include "battlespades/world/skylight_map.hpp"
#include "battlespades/world/static_light_field.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades;

void expect(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::vector<double> numbers(const std::string& text, std::size_t count) {
    std::vector<double> values;
    std::stringstream stream{text};
    std::string item;
    while (std::getline(stream, item, ',')) {
        values.push_back(std::stod(item));
    }
    expect(values.size() == count, "expected " + std::to_string(count) + " values in '" + text + "'");
    return values;
}

std::array<float, 3U> colour01(const std::string& text) {
    const auto values = numbers(text, 3U);
    return {static_cast<float>(values[0] / 255.0), static_cast<float>(values[1] / 255.0),
            static_cast<float>(values[2] / 255.0)};
}

std::array<float, 3U> vec3f(const std::string& text) {
    const auto values = numbers(text, 3U);
    return {static_cast<float>(values[0]), static_cast<float>(values[1]),
            static_cast<float>(values[2])};
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
        expect(args.contains("map") && args.contains("out") && args.contains("eye") &&
                   args.contains("ori"),
               "map=, out=, eye= and ori= are required");
        const auto width = static_cast<std::uint16_t>(std::stoi(get("width", "800")));
        const auto height = static_cast<std::uint16_t>(std::stoi(get("height", "600")));

        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Retail scene capture", width, height, SDL_WINDOW_HIDDEN);
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
        expect(ui.active_backend() == config.backend, "Requested graphics API was not selected");

        render::WorldRenderer scene;
        expect(scene.initialize(config.shader_root, config.asset_root, config.texture_quality),
               std::string{scene.last_error()});
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
        // probe=x,y,z: print the stored voxel colour/light word and exit.
        if (args.contains("probe")) {
            const auto at = numbers(args["probe"], 3U);
            const auto c = map.map->color(static_cast<std::uint32_t>(at[0]),
                                          static_cast<std::uint32_t>(at[1]),
                                          static_cast<std::uint32_t>(at[2]));
            std::cout << "probe solid=" << map.map->solid(static_cast<std::uint32_t>(at[0]),
                                                          static_cast<std::uint32_t>(at[1]),
                                                          static_cast<std::uint32_t>(at[2]));
            if (c.has_value()) {
                std::cout << " rgba=" << int{c->red} << ',' << int{c->green} << ','
                          << int{c->blue} << ',' << int{c->alpha};
            }
            std::cout << std::endl;
            return 0;
        }
        world::ChunkMesherConfig mesher_config;
        mesher_config.emissive = world::emissive_palette_for(map_name);
        // flares=x,y,z,r,g,b[;...]: server FlareBlockEntity static lights
        // (map markers). Each restores its coloured voxel and a radius-5
        // light, meshed with the retail kernel on the compatibility tier
        // exactly as the live remesher does.
        world::StaticLightField flares;
        if (args.contains("flares")) {
            std::stringstream list{args["flares"]};
            std::string item;
            while (std::getline(list, item, ';')) {
                if (item.empty()) {
                    continue;
                }
                const auto value = numbers(item, 6U);
                world::StaticLight light;
                light.cell = {static_cast<std::uint32_t>(value[0]),
                              static_cast<std::uint32_t>(value[1]),
                              static_cast<std::uint32_t>(value[2])};
                light.color = {static_cast<std::uint8_t>(value[3]),
                               static_cast<std::uint8_t>(value[4]),
                               static_cast<std::uint8_t>(value[5]), 255U};
                light.radius = world::StaticLightField::flare_block_radius;
                static_cast<void>(map.map->set_voxel(light.cell[0U], light.cell[1U],
                                                     light.cell[2U], light.color));
                static_cast<void>(flares.add(light));
            }
            mesher_config.static_lights = &flares;
            mesher_config.retail_static_light_kernel = get("tier", "compatibility") == "compatibility";
            if (mesher_config.retail_static_light_kernel) {
                mesher_config.emissive = {};
            }
        }
        if (args.contains("water")) {
            const auto water = numbers(args["water"], 3U);
            mesher_config.bed_water_color = {static_cast<std::uint8_t>(water[0]),
                                             static_cast<std::uint8_t>(water[1]),
                                             static_cast<std::uint8_t>(water[2]), 255U};
        }
        const world::ChunkMesher mesher{mesher_config};
        const auto per_axis = mesher.chunks_per_axis();
        // dump=<file>: raw ChunkVertex records of every chunk, for comparing
        // the mesher's retail atlas/light attributes against retail VBOs.
        std::ofstream dump;
        if (args.contains("dump")) {
            dump.open(args["dump"], std::ios::binary);
            expect(dump.good(), "cannot open dump file");
        }
        for (std::uint32_t y = 0; y < per_axis; ++y) {
            for (std::uint32_t x = 0; x < per_axis; ++x) {
                const auto mesh = mesher.mesh(*map.map, {x, y});
                if (dump.is_open() && !mesh.vertices.empty()) {
                    dump.write(reinterpret_cast<const char*>(mesh.vertices.data()),
                               static_cast<std::streamsize>(mesh.vertices.size() *
                                                            sizeof(world::ChunkVertex)));
                }
                expect(scene.upload_chunk(mesh), "chunk upload failed");
            }
        }
        dump.close();
        world::SkylightMap skylight;
        skylight.rebuild(*map.map);
        scene.set_skylight_horizon(skylight.data(), world::SkylightMap::edge);
        scene.set_retail_sea_color(
            render::retail_sea_color_for(*map.map, mesher_config.bed_water_color));

        const auto tier_name = get("tier", "compatibility");
        const auto tier = settings::parse_shader_quality(tier_name);
        expect(tier.has_value(), "unknown tier " + tier_name);
        scene.set_quality_profile(render::profile_for(*tier, settings::QualityLevel::medium));

        std::string dome = get("skydome", "");
        if (dome.empty()) {
            world::SkydomeConfidence confidence{};
            dome = world::resolve_map_skydome(config.asset_root, map_name, confidence);
        }
        const auto fog = numbers(get("fog", "128,128,128"), 3U);
        const std::array<std::uint8_t, 3U> fog_bytes{static_cast<std::uint8_t>(fog[0]),
                                                     static_cast<std::uint8_t>(fog[1]),
                                                     static_cast<std::uint8_t>(fog[2])};
        scene.set_fog_color(fog_bytes);
        expect(scene.set_skydome(dome), std::string{scene.last_error()});
        auto atmosphere = world::resolve_map_atmosphere(config.asset_root, dome);
        atmosphere.fog_color = fog_bytes;
        scene.set_atmosphere(atmosphere);
        scene.set_fog_color(fog_bytes);
        scene.set_retail_fog_color(fog_bytes);

        render::RetailTerrainLighting lighting;
        if (args.contains("light")) lighting.light_color = colour01(args["light"]);
        if (args.contains("light_dir")) lighting.light_direction = vec3f(args["light_dir"]);
        if (args.contains("back")) lighting.back_light_color = colour01(args["back"]);
        if (args.contains("back_dir")) lighting.back_light_direction = vec3f(args["back_dir"]);
        if (args.contains("ambient")) lighting.ambient_color = colour01(args["ambient"]);
        if (args.contains("ambient_intensity"))
            lighting.ambient_intensity = std::stof(args["ambient_intensity"]);
        scene.set_retail_lighting(lighting);

        render::WorldCamera camera;
        const auto eye = numbers(get("eye", ""), 3U);
        camera.eye = {eye[0], eye[1], eye[2]};
        const auto ori = numbers(get("ori", ""), 3U);
        const double horizontal = std::hypot(ori[0], ori[1]);
        constexpr double degrees = 180.0 / std::numbers::pi;
        // forward = (-cos yaw cos pitch, -sin yaw cos pitch, sin pitch)
        camera.yaw_degrees = std::atan2(-ori[1], -ori[0]) * degrees;
        camera.pitch_degrees = std::atan2(ori[2], horizontal) * degrees;
        camera.fov_y_degrees = std::stod(get("fov", "75"));
        camera.fog_distance = std::stod(get("fog_distance", "192"));
        camera.classic075_fog = get("classic075", "0") == "1";
        if (camera.classic075_fog) camera.fog_distance = std::min(camera.fog_distance, 128.0);
        if (args.contains("sky")) {
            const auto sky = numbers(args["sky"], 3U);
            camera.sky_anchor = std::array<double, 3U>{sky[0], sky[1], sky[2]};
        }

        world::ParticleSystem particles;
        if (args.contains("explosion_tool")) {
            expect(args.contains("effect_position"), "effect_position= required for explosion");
            const int tool = std::stoi(args["explosion_tool"]);
            const int ticks = std::stoi(get("effect_ticks", "0"));
            expect(tool >= 0 && tool <= 255, "explosion_tool must be a byte");
            expect(ticks >= 0 && ticks <= 600, "effect_ticks must be 0..600");
            const auto position = vec3f(args["effect_position"]);
            expect(std::ranges::all_of(position, [](float value) {
                return std::isfinite(value) && value >= 0.0F && value < 512.0F;
            }), "effect_position must be finite map coordinates");
            const auto effect_color = numbers(get("effect_color", "48,48,48"), 3U);
            expect(std::ranges::all_of(effect_color, [](double value) {
                return std::isfinite(value) && value >= 0.0 && value <= 255.0;
            }), "effect_color must contain bytes");
            world::TerrainImpactEvent impact{
                world::TerrainImpactKind::explosion,
                {static_cast<std::uint32_t>(position[0U]),
                 static_cast<std::uint32_t>(position[1U]),
                 static_cast<std::uint32_t>(position[2U])},
                {static_cast<std::uint8_t>(effect_color[0U]),
                 static_cast<std::uint8_t>(effect_color[1U]),
                 static_cast<std::uint8_t>(effect_color[2U]), 255U},
                {0, 0, -1}, true, 4.0F, static_cast<std::uint8_t>(tool), position};
            particles.set_gravity(std::stof(get("effect_gravity", "1")));
            world::emit_explosion(particles, impact);
            if (get("sticky_fragments", "0") == "1") {
                expect(tool == 57, "sticky_fragments requires explosion_tool=57");
                std::string model_error;
                const auto model = world::Kv6Model::load_file(
                    config.asset_root / "kv6" / "stickygrenade.kv6", &model_error);
                expect(model.has_value(), model_error);
                const auto* definition = world::find_entity_definition(35U);
                expect(definition != nullptr && !definition->parts.empty(),
                       "missing sticky entity definition");
                world::LocalEntity sticky;
                sticky.type = 35U;
                sticky.position = {position[0U], position[1U], position[2U]};
                world::emit_sticky_model_explosion(
                    particles, *model,
                    world::entity_presentation_transform(
                        sticky, *definition, definition->parts.front()),
                    0x571C168U);
            }
            for (int tick{}; tick < ticks; ++tick) {
                if (get("effect_collision", "1") == "0") {
                    particles.tick_unbounded(1.0 / 60.0);
                } else {
                    particles.tick(1.0 / 60.0, *map.map);
                }
            }
            particles.build_draw_list(
                {static_cast<float>(eye[0U]), static_cast<float>(eye[1U]),
                 static_cast<float>(eye[2U])}, static_cast<float>(camera.fog_distance));
            std::cout << "explosion_tool=" << tool << " effect_ticks=" << ticks
                      << " particles=" << particles.live_count() << '\n';
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
        // Two frames: the first uploads resources and settles the skydome.
        for (int pass = 0; pass < 3; ++pass) {
            expect(ui.begin_frame(), std::string{ui.last_error()});
            expect(scene.submit(camera, config.drawable_extent, {}, {},
                                particles.instances(), particles.batches()),
                   std::string{scene.last_error()});
            for (const auto id : {render::backdrop_clear_view_id, render::world_view_id,
                                  render::view_model_view_id, render::ui_window_view_id,
                                  render::ui_canvas_view_id}) {
                bgfx::setViewFrameBuffer(id, framebuffer);
            }
            bgfx::setViewFrameBuffer(render::ui_canvas_view_id + 1, BGFX_INVALID_HANDLE);
            bgfx::setViewRect(render::ui_canvas_view_id + 1, 0, 0, width, height);
            bgfx::touch(render::ui_canvas_view_id + 1);
            expect(ui.end_frame(), std::string{ui.last_error()});
            bgfx::blit(render::ui_canvas_view_id + 2, readback, 0, 0, color, 0, 0, width, height);
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
                auto last = pixels.begin() + static_cast<std::ptrdiff_t>((height - row - 1U) * stride);
                std::swap_ranges(first, first + static_cast<std::ptrdiff_t>(stride), last);
            }
        }
        for (std::size_t index = 3; index < pixels.size(); index += 4U) {
            pixels[index] = 255U;
        }
        const auto out = get("out", "");
        expect(stbi_write_png(out.c_str(), width, height, 4, pixels.data(), width * 4) != 0,
               "PNG write failed");
        std::cout << "wrote " << out << " yaw=" << camera.yaw_degrees
                  << " pitch=" << camera.pitch_degrees << " dome=" << dome << '\n';

        scene.shutdown();
        bgfx::destroy(readback);
        bgfx::destroy(framebuffer);
        bgfx::destroy(depth);
        bgfx::destroy(color);
        ui.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
