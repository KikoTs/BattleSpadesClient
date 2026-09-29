// Offscreen A/B capture of third-person characters and their attachments.
//
// Drives the production WorldRenderer exactly like the frontend: the same
// class/jetpack KV6 meshes, the same Character.draw child transforms, on a
// synthetic flat field. `mode=before` reproduces the pre-2026-09-27 native
// presentation (full team colour, 0.90/1.08 albedo fudge, 1.85 whole-model
// spawn flash, +0.1 standing upper-body lift, no living pack, no back intel,
// no classic corpse). `mode=after` is the retail Character.draw path.
//
// Usage: aos_character_scene_capture out=<file.png> [mode=after|before]
//            [view=front|back] [width=1280] [height=720]
//            [assets=<root>] [shaders=<bin root>] [backend=direct3d11]

#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/quality_profile.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/class_models.hpp"
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

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades;
using Mat4 = std::array<float, 16U>;

void expect(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

Mat4 identity() {
    return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (std::size_t row{}; row < 4U; ++row)
        for (std::size_t column{}; column < 4U; ++column) {
            float sum{};
            for (std::size_t k{}; k < 4U; ++k) sum += a[row * 4U + k] * b[k * 4U + column];
            r[row * 4U + column] = sum;
        }
    return r;
}
Mat4 translate(float x, float y, float z) {
    auto r = identity();
    r[12U] = x;
    r[13U] = y;
    r[14U] = z;
    return r;
}
Mat4 scale(float s) {
    auto r = identity();
    r[0U] = s;
    r[5U] = s;
    r[10U] = s;
    return r;
}
Mat4 rotate_x(float degrees) {
    const auto a = static_cast<float>(degrees * std::numbers::pi / 180.0);
    auto r = identity();
    r[5U] = std::cos(a);
    r[6U] = std::sin(a);
    r[9U] = -std::sin(a);
    r[10U] = std::cos(a);
    return r;
}
Mat4 rotate_z(float degrees) {
    const auto a = static_cast<float>(degrees * std::numbers::pi / 180.0);
    auto r = identity();
    r[0U] = std::cos(a);
    r[1U] = std::sin(a);
    r[4U] = -std::sin(a);
    r[5U] = std::cos(a);
    return r;
}

/** Character.draw DisplayList(model, z_offset=6) child. */
Mat4 child(const world::RetailJetpackAttachment& attachment, const Mat4& root) {
    const auto d = world::retail_display_vector({0.0, attachment.y, attachment.z});
    auto m = scale(static_cast<float>(attachment.size));
    m = mul(m, translate(static_cast<float>(d.x), static_cast<float>(d.y),
                         static_cast<float>(d.z)));
    m = mul(m, rotate_x(-90.0F));
    return mul(m, root);
}

struct Character final {
    std::uint8_t class_id{};
    std::uint8_t team{2U};
    bool crouched{};
    std::optional<std::uint8_t> jetpack;
    bool back_intel{};
    bool classic_corpse{};
    bool spawn_flash{};
};

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
        const bool before = get("mode", "after") == "before";
        const bool back_view = get("view", "back") == "back";
        const auto width = static_cast<std::uint16_t>(std::stoi(get("width", "1280")));
        const auto height = static_cast<std::uint16_t>(std::stoi(get("height", "720")));

        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Character capture", width, height, SDL_WINDOW_HIDDEN);
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

        // Flat grass field: z 60 is the ground surface.
        constexpr std::uint32_t ground_z{60U};
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
        for (std::uint32_t y = 224U; y < 288U; ++y)
            for (std::uint32_t x = 224U; x < 300U; ++x)
                for (std::uint32_t z = ground_z; z < ground_z + 3U; ++z)
                    static_cast<void>(map.set_voxel(
                        x, y, z,
                        world::VxlColor{static_cast<std::uint8_t>(96U + ((x + y) & 1U) * 6U),
                                        118U, 72U, 255U}));
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
        const std::array<std::uint8_t, 3U> fog{128U, 160U, 192U};
        expect(scene.set_skydome(dome), std::string{scene.last_error()});
        auto atmosphere = world::resolve_map_atmosphere(config.asset_root, dome);
        atmosphere.fog_color = fog;
        scene.set_atmosphere(atmosphere);
        scene.set_fog_color(fog);
        scene.set_retail_fog_color(fog);
        scene.set_retail_lighting(render::RetailTerrainLighting{});

        // Lineup: E2/E13 soldiers, E1 living packs (standing + crouched),
        // E3 Classic CTF back intel (no pack / JETPACK_NORMAL), E4 classic
        // corpse, E6 spawn flash.
        const std::vector<Character> lineup{
            {0U, 2U},
            {0U, 3U},
            {2U, 2U, false, std::uint8_t{66U}},
            {12U, 3U, true, std::uint8_t{68U}},
            {5U, 2U, false, std::nullopt, true},
            {2U, 3U, false, std::uint8_t{66U}, true},
            {5U, 2U, false, std::nullopt, false, true},
            {0U, 2U, false, std::nullopt, false, false, true},
        };
        const auto team_color = [](std::uint8_t team) {
            return team == 3U ? world::VxlColor{137U, 179U, 44U, 255U}
                              : world::VxlColor{44U, 117U, 179U, 255U};
        };
        const auto asset_root = std::filesystem::path{config.asset_root};
        std::uint32_t next_slot{96U};
        std::vector<render::WorldModelDraw> draws;
        const auto upload = [&](const world::ChunkMesh& mesh) {
            const auto slot = next_slot++;
            expect(scene.set_world_model_mesh(slot, mesh), std::string{scene.last_error()});
            return slot;
        };
        const auto attachment_mesh = [&](std::string_view stem, world::VxlColor colour) {
            std::string error;
            auto model = world::Kv6Model::load_file(
                asset_root / "kv6" / (std::string{stem} + ".kv6"), &error);
            expect(model.has_value(), error);
            model->offset_pivots({0.0F, 0.0F, 6.0F});
            model->apply_default_color(colour);
            return model->mesh();
        };
        const float spacing = 2.4F;
        const float first_x = 262.0F - spacing * static_cast<float>(lineup.size() - 1U) * 0.5F;
        // Yaw 0 faces +y; every character faces -y, towards the front camera.
        constexpr float yaw{180.0F};
        for (std::size_t index{}; index < lineup.size(); ++index) {
            const auto& character = lineup[index];
            const auto full = team_color(character.team);
            const auto half = world::retail_character_color(full);
            const auto model_color =
                before ? full
                       : (character.spawn_flash ? world::retail_spawn_flash_color(half) : half);
            auto models = world::load_class_models(asset_root, character.class_id, model_color);
            expect(static_cast<bool>(models), models.error);
            const float x = first_x + spacing * static_cast<float>(index);
            // Character root: the eye-level origin 2.25 blocks over the feet
            // (crouched 1.35) as the frontend passes motion.position.
            const float z = static_cast<float>(ground_z) - (character.crouched ? 1.35F : 2.25F);
            auto root = mul(rotate_z(yaw), translate(x, 256.0F, z));
            const auto& set = *models.models;
            const std::size_t first_draw = draws.size();
            if (character.classic_corpse) {
                if (!before) {
                    draws.push_back({upload(attachment_mesh(world::retail_classic_corpse_model,
                                                            half)),
                                     child(world::retail_classic_corpse_attachment(), root)});
                }
                continue;
            }
            // Pre-fix class meshes baked a -0.1 (upward) head/torso lift.
            const auto lift = before && !character.crouched ? translate(0.0F, 0.0F, -0.1F)
                                                            : identity();
            draws.push_back({upload(character.crouched ? set.crouching_torso_preview
                                                       : set.standing_torso_preview),
                             mul(lift, root)});
            draws.push_back({upload(set.head_preview), mul(lift, root)});
            draws.push_back({upload(character.crouched ? set.crouching_left_leg_preview
                                                       : set.left_leg_preview),
                             root});
            draws.push_back({upload(character.crouched ? set.crouching_right_leg_preview
                                                       : set.right_leg_preview),
                             root});
            if (!before && character.back_intel) {
                const auto other = world::retail_character_color(
                    team_color(character.team == 2U ? 3U : 2U));
                draws.push_back(
                    {upload(attachment_mesh(world::retail_back_intel_model, other)),
                     child(world::retail_back_intel_attachment(character.crouched,
                                                               character.jetpack),
                           root)});
            }
            if (!before && character.jetpack.has_value()) {
                const auto pack_color =
                    character.back_intel
                        ? world::retail_character_color(
                              team_color(character.team == 2U ? 3U : 2U))
                        : model_color;
                draws.push_back(
                    {upload(attachment_mesh(world::retail_jetpack_model(*character.jetpack),
                                            pack_color)),
                     child(world::retail_jetpack_attachment(), root)});
            }
            if (before) {
                for (std::size_t draw = first_draw; draw < draws.size(); ++draw) {
                    draws[draw].albedo_gain = character.spawn_flash ? 1.85F : 0.90F;
                    draws[draw].albedo_contrast = character.spawn_flash ? 0.85F : 1.08F;
                }
            }
        }

        render::WorldCamera camera;
        camera.eye = {262.0, back_view ? 266.5 : 245.5, static_cast<double>(ground_z) - 3.2};
        camera.yaw_degrees = back_view ? 90.0 : -90.0;
        camera.pitch_degrees = -8.0;
        camera.fov_y_degrees = std::stod(get("fov", "60"));
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
