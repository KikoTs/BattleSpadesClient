#pragma once

#include "battlespades/render/texture_quality.hpp"

#include "battlespades/render/quality_profile.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/dynamic_light.hpp"
#include "battlespades/world/map_atmosphere.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/sniper_laser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
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
    double near_plane{0.1};
    /** Radial fog end; the retail Draw Distance setting in blocks. */
    double fog_distance{192.0};
};

struct WorldFrameStats final {
    std::size_t chunks_resident{};
    std::size_t chunks_submitted{};
};

/**
 * The six lighting values carried by authoritative StateData(45).
 *
 * Directions remain in retail GL/render coordinates because the recovered
 * map shader consumes them in that basis. Colors are linearized only by the
 * original byte normalization; the Legacy path intentionally performs no
 * HDR/exposure transform.
 */
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

    /** Installs packet-45's exact two directional lights and ambient fill. */
    void set_retail_lighting(const RetailTerrainLighting& lighting) noexcept;
    [[nodiscard]] const RetailTerrainLighting& retail_lighting() const noexcept;

    /**
     * The active renderer quality profile, resolved by the caller.
     *
     * `enhanced_lighting` selects between the recovered baked face/occlusion
     * tables (Legacy, so retail screenshot parity stays runnable) and real
     * per-pixel directional lighting. Both read the same meshes, so switching
     * costs no re-mesh. The remaining fields are stored and reported now and
     * consumed by the HDR, shadow, ambient-occlusion and bloom passes as those
     * land; until then they change no pixels.
     *
     * Tier policy lives in render::profile_for(), so this class never has to
     * know what a tier name means.
     */
    void set_quality_profile(const QualityProfile& profile) noexcept;
    [[nodiscard]] const QualityProfile& quality_profile() const noexcept;

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
    static constexpr std::uint32_t view_model_slot_count{10U};
    /** Uploads or replaces one viewmodel mesh slot. */
    [[nodiscard]] bool set_view_model_mesh(std::uint32_t slot,
                                           const world::ChunkMesh& mesh);
    void clear_view_model() noexcept;

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
    static constexpr std::uint32_t world_model_slot_count{1920U};
    static constexpr std::uint32_t terrain_effect_slot_base{24U};
    static constexpr std::uint32_t terrain_effect_slot_count{40U};
    static constexpr std::uint32_t projectile_slot_base{64U};
    static constexpr std::uint32_t projectile_slot_count{32U};
    /** Budgeted per PART: a turret is three meshes and a UGC marker is two. */
    static constexpr std::uint32_t entity_slot_base{1664U};
    static constexpr std::uint32_t entity_slot_count{256U};
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
