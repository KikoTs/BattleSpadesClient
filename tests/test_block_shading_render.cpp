#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/camera_basis.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"
#include "battlespades/world/skylight_map.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace battlespades;
constexpr std::uint16_t size = 512;
void expect(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
world::ChunkMesh plane(std::uint8_t face) {
    world::ChunkMesh mesh;
    const auto axis = face / 2U;
    const auto u = (axis + 1U) % 3U;
    const auto v = (axis + 2U) % 3U;
    constexpr std::array<float, 3> center{256, 256, 128};
    mesh.minimum = mesh.maximum = center;
    mesh.minimum[u] -= 40; mesh.minimum[v] -= 40;
    mesh.maximum[u] += 40; mesh.maximum[v] += 40;
    // Real voxel-sized faces exercise derivatives on small triangles, including
    // helper lanes outside the primitive and both signs of every face normal.
    for (int y = -40; y < 40; ++y) for (int x = -40; x < 40; ++x) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& corner : std::array<std::array<int, 2>, 4>{{{0,0},{1,0},{1,1},{0,1}}}) {
            auto p = center;
            p[u] += static_cast<float>(x + corner[0]);
            p[v] += static_cast<float>(y + corner[1]);
            mesh.vertices.push_back({p[0],p[1],p[2],0x00808080,face});
        }
        for (const auto index : {0U,1U,2U,0U,2U,3U}) mesh.indices.push_back(base + index);
    }
    return mesh;
}
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "tmp/block-shading";
        std::filesystem::create_directories(output);
        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Block shading regression", size, size, SDL_WINDOW_HIDDEN);
        expect(window != nullptr, SDL_GetError());
        render::BgfxUiRenderer ui;
        render::BgfxUiRendererConfig config;
        config.native_window.window = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.asset_root = AOS_TEST_ASSET_ROOT;
        config.shader_root = argc > 3 ? argv[3] : AOS_SHADER_BIN_ROOT;
        config.drawable_extent = {size,size};
        const std::string_view backend = argc > 4 ? argv[4] : "direct3d11";
        expect(backend == "direct3d11" || backend == "direct3d12" || backend == "vulkan" || backend == "opengl",
               "Unknown graphics backend");
        config.backend = backend == "direct3d12" ? render::GraphicsBackend::direct3d12
            : backend == "vulkan" ? render::GraphicsBackend::vulkan
            : backend == "opengl" ? render::GraphicsBackend::opengl : render::GraphicsBackend::direct3d11;
        config.vertical_sync = false;
        const auto samples = static_cast<std::uint8_t>(argc > 5 ? std::stoi(argv[5]) : 4);
        expect(samples == 0 || samples == 2 || samples == 4, "Unsupported AA selection");
        config.multisample_samples = samples;
        const int texture_tier = argc > 6 ? std::stoi(argv[6]) : 2;
        expect(texture_tier >= 0 && texture_tier <= 2, "Unsupported texture selection");
        config.texture_quality = texture_tier == 0 ? render::TextureQualityTier::low
            : texture_tier == 1 ? render::TextureQualityTier::medium : render::TextureQualityTier::high;
        expect(ui.initialize(config), std::string{ui.last_error()});
        expect(ui.active_backend() == config.backend,"Requested graphics backend was not selected");
        render::WorldRenderer scene;
        expect(scene.initialize(config.shader_root, config.asset_root, config.texture_quality), std::string{scene.last_error()});
        scene.set_model_culling(false); // views are overridden after submit

        const std::uint64_t target_flags = samples == 4 ? BGFX_TEXTURE_RT_MSAA_X4
            : samples == 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT;
        const auto color = bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::RGBA8,
            target_flags);
        const auto depth = bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::D24S8,
            target_flags | BGFX_TEXTURE_RT_WRITE_ONLY);
        const std::array attachments{color,depth};
        const auto framebuffer = bgfx::createFrameBuffer(2,attachments.data(),false);
        const auto readback = bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        expect(bgfx::isValid(framebuffer) && bgfx::isValid(readback), "Capture target failed");
        std::array<float,16> view{}, projection{};
        render::WorldCamera camera;
        camera.eye = {256,256,100}; camera.fog_distance = 256;
        auto profile = render::profile_for(settings::ShaderQuality::ultra,settings::QualityLevel::high);
        const auto capture = [&](std::span<const render::WorldModelDraw> draws, bool shadows,
                                 std::span<const render::ViewModelDraw> held = {}) {
            auto selected = profile;
            if (!shadows) selected.shadow_cascades = 0;
            scene.set_quality_profile(selected);
            expect(ui.begin_frame(),std::string{ui.last_error()});
            expect(scene.submit(camera,config.drawable_extent,held,draws),std::string{scene.last_error()});
            for (const auto id : {render::backdrop_clear_view_id,render::world_view_id,
                    render::view_model_view_id,render::ui_window_view_id,render::ui_canvas_view_id})
                bgfx::setViewFrameBuffer(id,framebuffer);
            bgfx::setViewTransform(render::world_view_id,view.data(),projection.data());
            // GL/Vulkan resolve an offscreen MSAA attachment when the render
            // pass switches framebuffer. A blit alone does not trigger that.
            bgfx::setViewFrameBuffer(render::ui_canvas_view_id + 1,BGFX_INVALID_HANDLE);
            bgfx::setViewRect(render::ui_canvas_view_id + 1,0,0,size,size);
            bgfx::touch(render::ui_canvas_view_id + 1);
            expect(ui.end_frame(),std::string{ui.last_error()});
            bgfx::blit(render::ui_canvas_view_id + 2,readback,0,0,color,0,0,size,size);
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size)*size*4);
            const auto ready = bgfx::readTexture(readback,pixels.data());
            bool done = false;
            for (unsigned frame = 0; frame < 32; ++frame)
                if (bgfx::frame() >= ready) { done = true; break; }
            expect(done,"GPU readback timed out");
            if (bgfx::getCaps()->originBottomLeft) {
                constexpr std::size_t stride = static_cast<std::size_t>(size)*4;
                for (std::size_t row=0; row<size/2; ++row) {
                    auto first=pixels.begin()+static_cast<std::ptrdiff_t>(row*stride);
                    auto last=pixels.begin()+static_cast<std::ptrdiff_t>((size-row-1)*stride);
                    std::swap_ranges(first,first+static_cast<std::ptrdiff_t>(stride),last);
                }
            }
            return pixels;
        };
        const auto save = [&](const std::string& name,const std::vector<std::uint8_t>& pixels) {
            expect(stbi_write_png((output/(name+".png")).string().c_str(),size,size,4,pixels.data(),size*4) != 0,
                   "Capture write failed");
        };
        world::MapAtmosphere atmosphere;
        atmosphere.fog_density = 0; atmosphere.specular_strength = 0;
        atmosphere.key_intensity = 1; atmosphere.ambient_intensity = .25F;
        scene.set_atmosphere(atmosphere);
        std::size_t worst_acne{};
        for (std::uint8_t face = 0; face < 6; ++face) {
            expect(scene.set_world_model_mesh(0,plane(face)),std::string{scene.last_error()});
            const auto axis = face / 2U;
            std::array<float,3> eye{256,256,128};
            eye[axis] += (face % 2U == 0 ? -32.0F : 32.0F);
            eye[(axis+1U)%3U] += 12;
            const bx::Vec3 up = axis == 2 ? bx::Vec3{0,-1,0} : bx::Vec3{0,0,-1};
            bx::mtxLookAt(view.data(),{eye[0],eye[1],eye[2]},{256,256,128},up,bx::Handedness::Right);
            bx::mtxOrtho(projection.data(),-12,12,-12,12,.1F,100,0,bgfx::getCaps()->homogeneousDepth,bx::Handedness::Right);
            const std::array draws{render::WorldModelDraw{0}};
            for (const auto sun : std::array<std::array<float,3>,6>{{
                    {.36F,.26F,-.90F},{.92F,.01F,-.38F},{-.01F,-.94F,-.34F},
                    // Egypt's authored sun, exactly edge-on, and the other side
                    // of zero all used to produce different self-shadow noise.
                    {-.8660254F,3.1999e-8F,-.5F},{-.8660254F,0,-.5F},{-.8660254F,-.00011F,-.5F}}}) {
                atmosphere.sun_direction = sun; scene.set_atmosphere(atmosphere);
                const auto lit = capture(draws,false);
                const auto shaded = capture(draws,true);
                expect(lit[(size/2*size+size/2)*4] > 20 && shaded[(size/2*size+size/2)*4] > 10,
                       "Flat-face capture did not render");
                std::size_t acne{};
                int maximum{};
                for (std::size_t y=64; y<size-64; ++y) for (std::size_t x=64; x<size-64; ++x) {
                    const auto p=(y*size+x)*4;
                    const int error=std::abs(static_cast<int>(lit[p])-shaded[p]);
                    maximum=std::max(maximum,error);
                    if(error>3) ++acne;
                }
                worst_acne=std::max(worst_acne,acne);
                std::cout<<"face="<<static_cast<int>(face)<<" sun="<<sun[0]<<","<<sun[1]<<","<<sun[2]
                         <<" acne="<<acne<<" maximum="<<maximum<<std::endl;
                save("face-"+std::to_string(face)+"-sun-"+std::to_string(sun[0])+"-"+std::to_string(sun[1]),shaded);
            }
        }
        // Independent oracle transcribed from map_frag.py/model_frag.py, with
        // constant UVs so raster coverage cannot conceal the light-byte error.
        render::RetailTerrainLighting retail;
        retail.light_color = {.4F,.5F,.6F}; retail.light_direction = {0,1,0};
        retail.back_light_color = {.1F,.2F,.3F}; retail.back_light_direction = {1,0,0};
        retail.ambient_color = {.2F,.2F,.2F}; retail.ambient_intensity = .25F;
        scene.set_retail_lighting(retail);
        profile = render::profile_for(settings::ShaderQuality::compatibility,settings::QualityLevel::high);
        camera.yaw_degrees = 0; camera.pitch_degrees = 0;
        // ao_cube512 reaches retail GL bottom row first: atlas (0.375, 0.625)
        // is the neutral cell (red 255, no occlusion) and noise corner (0,0)
        // is the image's bottom-left texel. The earlier top-down upload read
        // 222..226 here, i.e. every block wore a mirrored AO cell.
        const double atlas_red = std::array{255.0,255.0,255.0}[static_cast<std::size_t>(texture_tier)] / 255.0;
        const double atlas_blue = std::array{235.0,236.0,239.0}[static_cast<std::size_t>(texture_tier)] / 255.0;
        const std::array draws{render::WorldModelDraw{0}};
        const auto center = (static_cast<std::size_t>(size)/2*size+size/2)*4;
        for (std::uint8_t face = 0; face < 6; ++face) {
            const auto axis = face / 2U;
            std::array<float,3> eye{256,256,128};
            eye[axis] += (face % 2U == 0 ? -32.0F : 32.0F);
            bx::mtxLookAt(view.data(),{eye[0],eye[1],eye[2]},{256,256,128},
                axis == 2 ? bx::Vec3{0,-1,0} : bx::Vec3{0,0,-1},bx::Handedness::Right);
            bx::mtxOrtho(projection.data(),-12,12,-12,12,.1F,100,0,bgfx::getCaps()->homogeneousDepth,bx::Handedness::Right);
            std::array<double,3> canonical{}; canonical[axis] = face%2 == 0 ? -1 : 1;
            for (bool model : {false,true}) for (const float baked : {0.0F,.25F,.5F,1.0F}) {
                // vxl.pyd sub_10030B60 writes the side-face normal codes with
                // x and y exchanged (+y face -> code 88 = GL +x, +x face ->
                // code 148 = GL +z), so map_vert decodes terrain normals as
                // (y, -z, x). KV6 model normals keep the plain (x, -z, y) basis.
                const std::array normal = model
                    ? std::array{canonical[0],-canonical[2],canonical[1]}
                    : std::array{canonical[1],-canonical[2],canonical[0]};
                auto mesh = plane(face);
                for (auto& vertex : mesh.vertices) {
                    vertex.static_light = model ? 0x40000000U : 0xFF000000U;
                    vertex.retail_baked_light = baked;
                    vertex.ao_u = model ? static_cast<float>(canonical[0]) : .375F;
                    vertex.ao_v = model ? static_cast<float>(canonical[1]) : .625F;
                    vertex.edge_u = model ? static_cast<float>(canonical[2]) : .375F;
                    vertex.edge_v = .625F;
                }
                expect(scene.set_world_model_mesh(0,mesh),std::string{scene.last_error()});
                const auto pixels = capture(draws,false);
                for (std::size_t channel=0;channel<3;++channel) {
                    const double key=retail.light_color[channel], back=retail.back_light_color[channel];
                    const double n0=normal[1], n1=normal[0];
                    const double h0=std::max(0.0,(normal[0]+normal[1])/std::sqrt(2.0));
                    const double h1=std::max(0.0,normal[0]);
                    const double albedo=(model ? 128.0 : std::floor(128.0*baked))/255.0;
                    double expected;
                    if (model) expected=albedo*std::clamp(.05+
                        (.75+.25*n0+.2*std::pow(h0,5))*key+
                        (.75+.25*n1+.2*std::pow(h1,5))*back,0.0,1.0);
                    else expected=std::clamp(.05+albedo*(std::max(.3,n0)*key+std::max(.3,n1)*back)+
                        .065*(std::pow(h0,10)*key+std::pow(h1,10)*back),0.0,1.0)*(atlas_red+.35)*atlas_blue;
                    expect(std::abs(pixels[center+channel]-expected*255.0) <= 2.0,
                        "Retail lighting oracle mismatch: face="+std::to_string(face)+" model="+std::to_string(model)+
                        " baked="+std::to_string(baked)+" expected="+std::to_string(expected*255.0)+
                        " actual="+std::to_string(pixels[center+channel]));
                }
            }
        }
        std::cout << "48 retail terrain/model lighting cases passed" << std::endl;
        // Exercise the production viewmodel pass as the camera turns. Predict
        // its lighting in world space so a missing/wrong view-space uniform
        // conversion cannot agree with the oracle by construction.
        world::ChunkMesh held_mesh;
        const std::array<float, 3> held_normal{.3F, .4F, std::sqrt(.75F)};
        for (const auto& xy : std::array<std::array<float, 2>, 4>{{
                {-.5F,-.5F},{.5F,-.5F},{.5F,.5F},{-.5F,.5F}}}) {
            world::ChunkVertex vertex;
            vertex.x=xy[0]; vertex.y=xy[1]; vertex.z=-1;
            vertex.abgr=0x00808080; vertex.static_light=0x40000000U;
            vertex.ao_u=held_normal[0]; vertex.ao_v=held_normal[1];
            vertex.edge_u=held_normal[2]; held_mesh.vertices.push_back(vertex);
        }
        held_mesh.indices={0,1,2,0,2,3};
        held_mesh.minimum={-.5F,-.5F,-1}; held_mesh.maximum={.5F,.5F,-1};
        expect(scene.set_view_model_mesh(0,held_mesh),std::string{scene.last_error()});
        const std::array held_draws{render::ViewModelDraw{0}};
        for (const double yaw : {0.0,90.0,180.0,270.0}) {
            for (const double pitch : {-45.0,0.0,45.0}) {
                camera.yaw_degrees=yaw; camera.pitch_degrees=pitch;
                const auto basis=render::world_camera_basis(yaw,pitch);
                std::array<double,3> normal{}, eye{};
                for (std::size_t axis=0;axis<3;++axis) {
                    normal[axis]=basis.right[axis]*held_normal[0]+
                        basis.up[axis]*held_normal[1]-basis.forward[axis]*held_normal[2];
                    eye[axis]=-basis.forward[axis];
                }
                normal={normal[0],-normal[2],normal[1]};
                eye={eye[0],-eye[2],eye[1]};
                const auto lighting=[&](const std::array<float,3>& light) {
                    double length{}, half_length{}, diffuse{}, specular{};
                    for (std::size_t axis=0;axis<3;++axis) length+=light[axis]*light[axis];
                    length=std::sqrt(length);
                    for (std::size_t axis=0;axis<3;++axis) {
                        const double direction=light[axis]/length;
                        diffuse+=normal[axis]*direction;
                        specular+=normal[axis]*(direction+eye[axis]);
                        half_length+=(direction+eye[axis])*(direction+eye[axis]);
                    }
                    return .75+.25*diffuse+.2*std::pow(std::max(0.0,specular/std::sqrt(half_length)),5);
                };
                const auto pixels=capture({},false,held_draws);
                for (std::size_t channel=0;channel<3;++channel) {
                    const double expected=128.0*std::clamp(.05+
                        lighting(retail.light_direction)*retail.light_color[channel]+
                        lighting(retail.back_light_direction)*retail.back_light_color[channel],0.0,1.0);
                    expect(std::abs(pixels[center+channel]-expected)<=2.0,
                        "Viewmodel lighting rotated incorrectly at yaw="+std::to_string(yaw)+
                        " pitch="+std::to_string(pitch));
                }
            }
        }
        camera.yaw_degrees=0; camera.pitch_degrees=0;
        std::cout << "12 viewmodel camera rotations passed" << std::endl;
        // Classic has no second sun. A zero direction for its disabled light
        // must be indistinguishable from any other direction, on every API.
        retail.back_light_color = {0,0,0};
        scene.set_retail_lighting(retail);
        const auto disabled_light_reference = capture(draws,false,held_draws);
        retail.back_light_direction = {0,0,0};
        scene.set_retail_lighting(retail);
        expect(capture(draws,false,held_draws) == disabled_light_reference,
               "Zero-length disabled Classic light corrupts terrain or held model");
        retail.back_light_direction = {1,0,0};
        scene.set_retail_lighting(retail);
        // Cycle every tier/effect combination without remeshing, then restore Legacy.
        auto surface=plane(4);
        for (auto& vertex:surface.vertices) {
            vertex.static_light=0xFF000000U; vertex.ao_u=vertex.edge_u=.375F;
            vertex.ao_v=vertex.edge_v=.625F;
        }
        expect(scene.set_world_model_mesh(0,surface),std::string{scene.last_error()});
        bx::mtxLookAt(view.data(),{256,256,96},{256,256,128},{0,-1,0},bx::Handedness::Right);
        const auto legacy_reference=capture(draws,false);
        for (const auto tier:{settings::ShaderQuality::compatibility,settings::ShaderQuality::low,
                settings::ShaderQuality::medium,settings::ShaderQuality::high,settings::ShaderQuality::ultra})
            for (const auto effects:{settings::QualityLevel::low,settings::QualityLevel::medium,settings::QualityLevel::high}) {
                profile=render::profile_for(tier,effects);
                const auto pixels=capture(draws,true);
                expect(pixels[center] > 10 && pixels[center+3] == 255,"Tier/effect switch produced an invalid frame");
            }
        profile=render::profile_for(settings::ShaderQuality::compatibility,settings::QualityLevel::high);
        expect(capture(draws,false)==legacy_reference,"Tier switching retained stale lighting resources");
        expect(ui.set_presentation_options(true,samples) && ui.set_presentation_options(false,samples),
               "VSync presentation reset failed");
        expect(capture(draws,false)==legacy_reference,"Presentation reset changed Legacy lighting");
        scene.set_fog_color({3,19,237}); scene.set_retail_fog_color({211,23,7});
        camera.fog_distance=16;
        const auto fogged=capture(draws,false);
        expect(fogged[center]==211 && fogged[center+1]==23 && fogged[center+2]==7,
               "Legacy fog did not preserve the server color");
        camera.fog_distance=256;
        std::cout << "15 tier/effect combinations, presentation reset, and fog authority passed" << std::endl;

        // A visible plane 160 blocks away in XY must be completely fogged
        // in 0.75, even under enhanced lighting with a different sky horizon.
        camera.classic075_fog=true;
        camera.fog_distance=128;
        camera.eye={96,256,96};
        scene.set_fog_color({128,128,128});
        for (const auto tier : {settings::ShaderQuality::compatibility,settings::ShaderQuality::low,
                               settings::ShaderQuality::medium,settings::ShaderQuality::high,settings::ShaderQuality::ultra}) {
            profile=render::profile_for(tier,settings::QualityLevel::high);
            const auto pixels=capture(draws,false);
            expect(pixels[center]==128 && pixels[center+1]==128 && pixels[center+2]==128,
                   "0.75 visibility limit or fog color changed with shader quality");
        }
        camera.classic075_fog=false;
        camera.fog_distance=256;
        camera.eye={256,256,100};
        profile=render::profile_for(settings::ShaderQuality::compatibility,settings::QualityLevel::high);
        std::cout << "0.75 gray fog boundary passed at every shader tier" << std::endl;

        // draw_sea: one 2000-block quad just under the bed, lit by sea_frag
        // (normal up, AO and edge from the neutral cell, grain repeated 2000x,
        // per-fragment fog, then x1.01). Looking straight down at an empty
        // world leaves only the sea at the centre pixel.
        {
            scene.set_retail_sea_color(std::array<std::uint8_t,3>{40,54,64});
            const auto saved_eye=camera.eye;
            camera.eye={256,256,200};
            bx::mtxLookAt(view.data(),{256,256,200},{256,256,240},{0,-1,0},bx::Handedness::Right);
            bx::mtxOrtho(projection.data(),-12,12,-12,12,.1F,100,0,bgfx::getCaps()->homogeneousDepth,bx::Handedness::Right);
            const auto sea=capture({},false);
            const std::array<double,3> albedo{40/255.0,54/255.0,64/255.0};
            // The grain repeats once per block, so the exact texel under the
            // centre pixel is raster-dependent; it is one scalar shared by all
            // three channels. Fit it, require it to be a real ao_cube512 blue
            // value, then require every channel to match the sea_frag oracle.
            std::array<double,3> unscaled{};
            double numerator=0.0, denominator=0.0;
            for (std::size_t channel=0;channel<3;++channel) {
                const double key=retail.light_color[channel], back=retail.back_light_color[channel];
                const double combined=std::clamp(.05+albedo[channel]*(key+.3*back)+
                    .065*std::pow(1.0/std::sqrt(2.0),10)*key,0.0,1.0);
                // AO red and edge green are both 255 in the neutral cell.
                unscaled[channel]=combined*(1.0+.35)*1.01*255.0;
                numerator+=unscaled[channel]*sea[center+channel];
                denominator+=unscaled[channel]*unscaled[channel];
            }
            const double grain=numerator/denominator;
            expect(grain>.75 && grain<1.0,"Retail sea grain outside the atlas blue range: "+std::to_string(grain));
            for (std::size_t channel=0;channel<3;++channel) {
                expect(std::abs(sea[center+channel]-unscaled[channel]*grain)<=2.0,
                    "Retail sea mismatch: channel="+std::to_string(channel)+" expected="+
                    std::to_string(unscaled[channel]*grain)+" actual="+std::to_string(sea[center+channel]));
            }
            scene.set_retail_sea_color(std::nullopt);
            camera.eye=saved_eye;
            std::cout << "retail sea passed" << std::endl;
        }
        if (argc > 2 && std::string_view{argv[2]} != "-") {
            const auto map = world::VxlMap::load_file(argv[2]);
            expect(static_cast<bool>(map),"Map load failed");
            const world::ChunkMesher mesher;
            for (std::uint32_t y=0;y<32;++y) for(std::uint32_t x=0;x<32;++x)
                expect(scene.upload_chunk(mesher.mesh(*map.map,{x,y})),"Map upload failed");
            world::SkylightMap skylight; skylight.rebuild(*map.map);
            scene.set_skylight_horizon(skylight.data(),world::SkylightMap::edge);
            const auto egypt = world::resolve_map_atmosphere(config.asset_root,"Egypt.txt");
            scene.set_atmosphere(egypt);
            expect(scene.set_skydome("Egypt.txt"),std::string{scene.last_error()});
            scene.set_fog_color(egypt.fog_color);
            scene.set_retail_lighting(render::RetailTerrainLighting{});
            profile=render::profile_for(settings::ShaderQuality::ultra,settings::QualityLevel::high);
            std::cout<<"Egypt sun="<<egypt.sun_direction[0]<<","<<egypt.sun_direction[1]<<","<<egypt.sun_direction[2]<<std::endl;
            camera.eye={64.5,218.5,209.5}; camera.yaw_degrees=90; camera.pitch_degrees=0;
            bx::mtxLookAt(view.data(),{64.5F,218.5F,209.5F},{64.5F,184.5F,209.5F},{0,0,-1},bx::Handedness::Right);
            bx::mtxProj(projection.data(),75,1,.1F,300,bgfx::getCaps()->homogeneousDepth,bx::Handedness::Right);
            save("egypt-shadow",capture({},true));
            save("egypt-no-shadow",capture({},false));
            for (const auto tier:{settings::ShaderQuality::compatibility,settings::ShaderQuality::low,
                    settings::ShaderQuality::medium,settings::ShaderQuality::high,settings::ShaderQuality::ultra}) {
                profile=render::profile_for(tier,settings::QualityLevel::high);
                save("egypt-"+std::string{render::quality_profile_name(tier)},capture({},true));
            }
            camera.eye={342.5,207.5,203.5}; camera.yaw_degrees=-170; camera.pitch_degrees=15;
            const auto basis=render::world_camera_basis(camera.yaw_degrees,camera.pitch_degrees);
            const bx::Vec3 eye{342.5F,207.5F,203.5F};
            bx::mtxLookAt(view.data(),eye,{eye.x+static_cast<float>(basis.forward[0]),
                eye.y+static_cast<float>(basis.forward[1]),eye.z+static_cast<float>(basis.forward[2])},
                {0,0,-1},bx::Handedness::Right);
            save("cliff-shadow",capture({},true));
            save("cliff-no-shadow",capture({},false));
        }
        scene.shutdown(); bgfx::destroy(readback); bgfx::destroy(framebuffer);
        bgfx::destroy(depth); bgfx::destroy(color); ui.shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        expect(worst_acne <= 20,"A flat unoccluded block face shadows itself");
        std::cout<<"36 face/light cases passed; MSAA="<<static_cast<int>(samples)<<" texture="<<texture_tier
                 <<" worst self-shadow pixels="<<worst_acne<<'\n';
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
