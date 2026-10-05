#pragma once

#include "battlespades/render/texture_quality.hpp"

#include "battlespades/render/post_settings.hpp"
#include "battlespades/render/quality_profile.hpp"
#include "battlespades/render/render_tuning.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/dynamic_light.hpp"
#include "battlespades/world/emissive_volume.hpp"
#include "battlespades/world/map_atmosphere.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/sniper_laser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::render {

struct UiExtent;

/**
 * Perspective camera state in canonical map coordinates (z down).
 *
 * Angles use the retail degree-based look model: yaw 0 faces -x, pitch is
 * positive looking down (+z), forward =
 * (-cos yaw * cos pitch, -sin yaw * cos pitch, sin pitch).
 */
struct WorldCamera final {
    std::array<double, 3U> eye{};
    double yaw_degrees{};
    double pitch_degrees{};
    /** Recovered retail hip perspective: vertical 75 degrees, near 0.1. */
    double fov_y_degrees{75.0};
    /**
     * First-person tool projection; nullopt = fov_y_degrees. The frontend keeps
     * it at the retail 75-degree-based zoom FOV when the player widens the
     * world FOV, so the weapon never stretches and ADS sight images stay
     * aligned.
     */
    std::optional<double> view_model_fov_y_degrees{};
    double near_plane{0.1};
    /** Radial fog end; the retail Draw Distance setting in blocks. */
    double fog_distance{192.0};
    /**
     * Map position the authored skydome is centred on; unset follows the eye.
     *
     * Retail GameScene.draw always translates the dome to
     * camera.get_position(), but the view matrix comes from the active
     * controller or the character. With neither (LoadingMenu, SelectTeam and
     * SelectClass before create_player) the modelview stays identity, so the
     * eye sits at the map origin while the dome stays on the camera's
     * constructor position.
     */
    std::optional<std::array<double, 3U>> sky_anchor{};
};

struct WorldFrameStats final {
    std::size_t chunks_resident{};
    std::size_t chunks_submitted{};
    std::size_t shadow_chunks_submitted{};
    std::size_t shadow_chunks_culled{};
    /** Fullscreen post passes submitted; zero when the world drew straight to the backbuffer. */
    std::uint32_t post_passes{};
    /** Offscreen scene size while the post chain is active. */
    std::uint32_t post_scene_width{};
    std::uint32_t post_scene_height{};
};

/**
 * The six lighting values carried by authoritative StateData(45).
 *
 * Directions remain in retail GL/render coordinates because the recovered
 * map shader consumes them in that basis. Colors are linearized only by the
 * original byte normalization; the Legacy path intentionally performs no
 * HDR/exposure transform.
 */
/**
 * The retail sea colour: vxl.pyd draw_sea reads the voxel colour at (0,0,239),
 * which the map finaliser has made the map-wide bed colour. VxlMap stores that
 * bed as colour zero, so it resolves to the mesher's bed/water colour.
 */
[[nodiscard]] std::array<std::uint8_t, 3U> retail_sea_color_for(
    const world::VxlMap& map, world::VxlColor bed_water_color) noexcept;

struct RetailTerrainLighting final {
    std::array<float, 3U> light_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3U> light_direction{0.0F, 0.707F, -0.707F};
    std::array<float, 3U> back_light_color{0.25F, 0.25F, 0.25F};
    std::array<float, 3U> back_light_direction{0.0F, 0.707F, 0.707F};
    std::array<float, 3U> ambient_color{0.20F, 0.22F, 0.25F};
    float ambient_intensity{0.203125F};
};

/**
 * One first-person draw: a resident viewmodel mesh slot and its full
 * model->view matrix in GL view space (x right, y up, tool at negative z;
 * memory layout has the translation at [12..14]). The recovered
 * Character.draw_fps composites a frame from several of these: the held
 * tool plus the three soldier arm parts.
 */
struct ViewModelDraw final {
    std::uint32_t slot{};
    std::array<float, 16U> transform{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                     0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    /**
     * Retail PASSTHROUGH_SHADER: raw vertex colour, no lighting, back faces
     * culled as retail's GL_CULL_FACE does. Used by the first-person muzzle
     * flash (Weapon.draw_muzzle).
     */
    bool unlit{};
};

/** One resident multipart/class mesh placed in the normal world pass. */
struct WorldModelDraw final {
    std::uint32_t slot{};
    std::array<float, 16U> transform{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                     0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    /** Alpha for client-only placement ghosts; normal models stay opaque. */
    float opacity{1.0F};
    /** Optional client-side albedo treatment; characters use a restrained silhouette boost. */
    float albedo_gain{1.0F};
    float albedo_contrast{1.0F};
    /**
     * Which moving object this part belongs to, for render-rate interpolation
     * between fixed ticks (frontend/world_draw_interpolation.hpp); zero is
     * anonymous. The renderer never reads it: it changes no pixel of a frame
     * drawn from one tick's state.
     */
    std::uint32_t motion_key{};
    /**
     * Casts a sun shadow but is never drawn in the visible passes: the local
     * player's own body in first person, which the camera sits inside.
     */
    bool shadow_only{};
};

/** One retail sniper LaserAttachment already clipped against the live world. */
struct LaserBeamDraw final {
    world::SniperLaserPose pose;
};

/** Retail's terrain-projected soft contact shadow for selected entities. */
struct SpotShadowDraw final {
    std::array<float, 3U> position{};
    float size{1.0F};
    float opacity{0.72F};
};

/** Retail MinimapZone.draw_zone_cube additive objective boundary. */
struct ZoneVolumeDraw final {
    std::array<float, 3U> minimum{};
    std::array<float, 3U> maximum{};
    std::array<float, 3U> color{1.0F, 1.0F, 1.0F};
    float opacity{0.3F};
    /** Packet locked_in_zone maps to retail MinimapZone.solid. */
    bool solid{};
};

/**
 * Owner of the 3D voxel terrain pass on world_view_id.
 *
 * Separate from BgfxUiRenderer by design: it owns its own shader program,
 * vertex layout, uniforms and per-chunk static GPU buffers, and renders
 * between the UI backdrop clear and the UI sprite layers. bgfx must already
 * be initialized; every method is main-thread-only. upload_chunk() is the
 * only GPU entry point for mesh data, so callers control the per-frame
 * upload budget; meshes themselves come from worker threads.
 */
class WorldRenderer final {
public:
    WorldRenderer();
    ~WorldRenderer();

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    WorldRenderer(WorldRenderer&&) = delete;
    WorldRenderer& operator=(WorldRenderer&&) = delete;

    [[nodiscard]] bool initialize(const std::filesystem::path& shader_root,
                                  const std::filesystem::path& asset_root,
                                  TextureQualityTier texture_quality =
                                      TextureQualityTier::medium);
    void shutdown() noexcept;

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

    /**
     * Loads the layered retail skydome selected by server packet 51.
     *
     * The argument is a validated basename (`Tokyo.txt`), never a path. The
     * old layers remain active if parsing or GPU upload fails, so a malformed
     * UGC update cannot blank the world mid-frame.
     */
    [[nodiscard]] bool set_skydome(std::string_view definition_name);
    [[nodiscard]] std::string_view skydome_name() const noexcept;

    /** StateData(45) fog is also the terrain fade/clear fallback colour. */
    void set_fog_color(std::array<std::uint8_t, 3U> color) noexcept;
    /** Preserve packet fog in Legacy while Enhanced uses its measured sky horizon. */
    void set_retail_fog_color(std::array<std::uint8_t, 3U> color) noexcept;
    /**
     * The retail sea: vxl.pyd draw_sea's single 2000x2000 quad 0.1 below the
     * bed top, coloured with the map-wide z=239 bed colour. Drawn in the
     * Retail tier only; nullopt (the default) draws nothing.
     */
    void set_retail_sea_color(std::optional<std::array<std::uint8_t, 3U>> color) noexcept;

    /** Installs packet-45's exact two directional lights and ambient fill. */
    void set_retail_lighting(const RetailTerrainLighting& lighting) noexcept;
    [[nodiscard]] const RetailTerrainLighting& retail_lighting() const noexcept;

    /**
     * The active renderer quality profile, resolved by the caller.
     *
     * `enhanced_lighting` selects between recovered VXL/KV6 lighting (Legacy)
     * and enhanced directional lighting. Both read the same meshes, so
     * switching costs no re-mesh. Active profiles configure shadows, dynamic
     * lights, emission and particle lighting; reserved HDR/SSAO/bloom fields
     * stay zero until those passes exist.
     *
     * Tier policy lives in render::profile_for(), so this class never has to
     * know what a tier name means.
     */
    void set_quality_profile(const QualityProfile& profile) noexcept;
    /**
     * Main-pass world-model sphere culling against the submitted camera
     * (behind the eye plane / past fog). On by default; harnesses that
     * replace the world view transform after submit must turn it off.
     */
    void set_model_culling(bool enabled) noexcept;
    [[nodiscard]] const QualityProfile& quality_profile() const noexcept;

    /**
     * Optional world post-processing for the next submit() (post_settings.hpp).
     * At the defaults the world draws straight into the backbuffer as it
     * always has; otherwise it goes through an offscreen scene and fullscreen
     * passes resolved at composite_view_id, before every UI view. Features the
     * backend cannot run (post_capabilities()) are ignored.
     */
    void set_post_settings(const PostSettings& settings) noexcept;
    [[nodiscard]] const PostSettings& post_settings() const noexcept;
    /**
     * What this backend supports. Loads the post shaders on first call (never
     * at initialize(), so a missing post shader cannot break startup); a
     * failure only reports the chain unsupported. Main thread only.
     */
    [[nodiscard]] PostCapabilities post_capabilities() const noexcept;
    [[nodiscard]] bool post_chain_supported() const noexcept;

    /**
     * Sampling of world-space textures (skydome, particles, the terrain AO
     * atlas, laser and zone sprites): point (crisp) or linear, optionally
     * anisotropic at the device maximum. Applied live through per-draw sampler
     * flags; the bgfx reset flags never change for it.
     */
    void set_texture_filtering(const TextureFiltering& filtering) noexcept;

    /**
     * Selects the submission path (see RenderTuning); never the image.
     * `packed_terrain` applies to chunks uploaded after the call, so a
     * harness comparing both paths re-uploads its terrain.
     */
    void set_render_tuning(const RenderTuning& tuning) noexcept;
    [[nodiscard]] const RenderTuning& render_tuning() const noexcept;
    [[nodiscard]] TerrainMemory terrain_memory() const noexcept;

    /**
     * The per-map atmosphere driving enhanced lighting.
     *
     * set_skydome() derives this automatically from the map's own sky
     * gradient and sun layer, so terrain agrees with the sky above it without
     * any authoring. This setter exists for a server that art-directs its own
     * lighting and for tests.
     */
    void set_atmosphere(const world::MapAtmosphere& atmosphere) noexcept;
    [[nodiscard]] const world::MapAtmosphere& atmosphere() const noexcept;

    /**
     * Uploads the per-column skylight horizon that darkens interiors.
     *
     * Ambient light is hemispheric skylight, so a surface the sky cannot see
     * must not receive it; without this a bunker interior is lit as brightly as
     * its own roof. Call again whenever terrain changes the horizon. Failure is
     * non-fatal: the world simply renders without interior darkening.
     */
    void set_skylight_horizon(std::span<const std::uint8_t> horizon,
                              std::uint32_t edge) noexcept;

    /**
     * How much skylight reaches the player, for lighting the held weapon.
     *
     * The viewmodel is drawn in view space, so it cannot probe the skylight
     * horizon texture the way world geometry does -- its position is not a world
     * position. Without this it received full ambient while the room around it
     * was occluded, so the hands glowed in a dark interior. The caller knows
     * where the camera is and can sample the horizon on the CPU.
     */
    void set_viewmodel_skylight(float skylight) noexcept;

    /**
     * Non-owning map light sources that models sample on enhanced tiers.
     *
     * Terrain gets flare/fire light from its per-vertex bake and emissive
     * spill from the volume probe; KV6 models have no bake, and the view
     * model is drawn in view space where the probe reads the wrong cell. So
     * submit() samples these on the CPU at each world model's origin (placed
     * light) and at the eye for the view model (placed + spill) and adds the
     * result through u_modelLight, with terrain's own gains. Either pointer
     * may be null; both must outlive the next submit(). The Retail tier never
     * reads them: retail model_frag lights KV6s with packet 45 only.
     */
    void set_model_light_sources(const world::StaticLightField* placed,
                                 const world::EmissiveVolume* cast) noexcept;

    /**
     * Uploads the volume of light cast by emissive blocks.
     *
     * This is what makes a neon sign bleed onto the wall opposite it and a
     * lantern pool light on the road. Rebuilt at map load rather than per frame.
     * Non-fatal on failure: the world renders with self-illumination only.
     */
    void set_emissive_volume(std::span<const std::uint8_t> cells, std::uint32_t width,
                             std::uint32_t depth, std::uint32_t height) noexcept;
    /** Sets the authored map gain applied to attenuated emissive spill. */
    void set_emissive_cast_gain(float gain) noexcept;

    /** Creates or replaces the GPU buffers for one meshed chunk. */
    [[nodiscard]] bool upload_chunk(const world::ChunkMesh& mesh);
    void remove_chunk(world::ChunkKey key) noexcept;
    void clear_chunks() noexcept;
    [[nodiscard]] std::size_t resident_chunks() const noexcept;

    /**
     * Mesh slots for the first-person composite (tool + arm parts).
     *
     * Eight were exactly consumed by the tutorial's block/spade/pistol, two
     * arms and three parity-debug copies, leaving no room for the aimed
     * models. Character.draw_sight draws up to two more (the sight and the
     * classic rifle's pin), so the budget is ten.
     */
    static constexpr std::uint32_t view_model_slot_count{64U};
    /** Uploads or replaces one viewmodel mesh slot. */
    [[nodiscard]] bool set_view_model_mesh(std::uint32_t slot,
                                           const world::ChunkMesh& mesh);
    void clear_view_model() noexcept;
    /** True when a viewmodel slot holds uploaded buffers (cleared slots do not). */
    [[nodiscard]] bool view_model_mesh_resident(std::uint32_t slot) const noexcept;

    /** 0..23 debug, 24..63 effects, 64..95 projectiles, 96..1631 players. */
    // Slots 96..1631 hold 128 independent twelve-part remote-player rigs:
    // two torsos, head, four legs, three tool parts and two reusable arm
    // meshes. Retail articulates the head and crouched legs independently, so
    // folding them back into body composites would recreate the detached-hand
    // and fast/static-crouch animation regressions.
    //
    // Keep the entity band above the maximum valid player id (127). The 32
    // slot guard band makes a bad presentation index fail empty instead of
    // aliasing a pickup/turret model on a full server.
    static constexpr std::uint32_t world_model_slot_count{2240U};
    static constexpr std::uint32_t terrain_effect_slot_base{24U};
    static constexpr std::uint32_t terrain_effect_slot_count{40U};
    static constexpr std::uint32_t projectile_slot_base{64U};
    static constexpr std::uint32_t projectile_slot_count{32U};
    /** Budgeted per PART: a turret is three meshes and a UGC marker is two. */
    static constexpr std::uint32_t entity_slot_base{1664U};
    static constexpr std::uint32_t entity_slot_count{256U};
    /**
     * Shared retail effect meshes drawn many times per frame from one slot:
     * tracer KV6s, muzzleflash_default and Crate_Parachute.
     */
    static constexpr std::uint32_t effect_model_slot_base{1920U};
    static constexpr std::uint32_t effect_model_slot_count{64U};
    /**
     * Shared character accessory meshes keyed by model and colour, drawn by
     * any number of players: the living jetpack, the Classic CTF back intel,
     * ClassicCorpse and the spawn-protection (full team colour) body parts.
     */
    static constexpr std::uint32_t character_accessory_slot_base{1984U};
    static constexpr std::uint32_t character_accessory_slot_count{256U};
    static_assert(character_accessory_slot_base + character_accessory_slot_count ==
                  world_model_slot_count);
    /** Shader-side bounded forward-light array. */
    static constexpr std::size_t maximum_dynamic_lights{8U};
    [[nodiscard]] bool set_world_model_mesh(std::uint32_t slot,
                                            const world::ChunkMesh& mesh);
    void clear_world_models() noexcept;

    /**
     * Submits the world pass for the current frame: depth-cleared sky/fog
     * backdrop, frustum- and fog-distance-culled chunk draws, the blended
     * particle pass, then the depth-cleared first-person viewmodel pass
     * drawing every requested slot with its own matrix. Call between
     * BgfxUiRenderer begin_frame() and end_frame().
     *
     * Particles are submitted inside world_view_id after the opaque geometry
     * so they blend against the terrain rather than the sky. The view is
     * ViewMode::Sequential, so this submission order is the only ordering
     * guarantee; `batches` must already be sorted by ParticleSystem.
     */
    [[nodiscard]] bool submit(const WorldCamera& camera, UiExtent drawable,
                              std::span<const ViewModelDraw> view_model = {},
                              std::span<const WorldModelDraw> world_models = {},
                              std::span<const world::ParticleInstance> particles = {},
                              std::span<const world::ParticleBatch> particle_batches = {},
                              std::span<const world::DynamicLight> dynamic_lights = {},
                              std::span<const LaserBeamDraw> laser_beams = {},
                              std::span<const SpotShadowDraw> spot_shadows = {},
                              std::span<const ZoneVolumeDraw> zone_volumes = {});

    [[nodiscard]] WorldFrameStats last_frame_stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::render
