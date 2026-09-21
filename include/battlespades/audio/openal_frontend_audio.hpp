#pragma once

#include "battlespades/audio/audio_port.hpp"
#include "battlespades/audio/server_audio_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace battlespades::audio {

/** Startup settings for the retail-compatible frontend audio bed and cues. */
struct OpenAlFrontendAudioConfig final {
    std::filesystem::path asset_root{"assets/original"};
    std::size_t max_one_shot_voices{48U};
    /** Retail config.py music_volume default. */
    float music_gain{1.0F};
    float cue_gain{1.0F};
    bool play_music_on_start{true};
    /** Empty tries the system default, then available outputs; an explicit device never falls back. */
    std::string playback_device;
};

/** Pure validation helper; does not touch the filesystem or an audio device. */
[[nodiscard]] bool
valid_openal_frontend_audio_config(const OpenAlFrontendAudioConfig& config) noexcept;

/**
 * One recovered per-tool cue role, mirroring world::WeaponSoundSet.
 *
 * Order must match the bind sequence in start(); `count` sizes the per-tool
 * storage.
 */
enum class WeaponCue : std::uint8_t {
    fire_loop,
    fire_tail,
    spin_loop,
    melee_miss,
    melee_hit_block,
    melee_hit_player,
    empty_fire,
    pin,
    throw_release,
    tool_loop_start,
    tool_loop,
    tool_loop_stop,
    tool_extra,
    count,
};

/**
 * Handle to a sustained looping voice; 0 is never valid.
 *
 * Loops come from a dedicated source pool rather than the one-shot voices,
 * because one-shot acquisition steals the oldest voice and would cut a
 * sustained machine-gun loop off mid-burst.
 */
using LoopVoice = std::uint32_t;
inline constexpr LoopVoice invalid_loop_voice{0U};

/** Main-thread terrain transmission callback for positional audio. */
using SpatialGainResolver = float (*)(void* context,
                                      SoundPosition listener,
                                      SoundPosition source) noexcept;

/**
 * Main-thread OpenAL Soft owner for frontend music and UI cues.
 *
 * The module opens one default playback device/context, decodes the three
 * immutable retail OGG assets into cached OpenAL buffers, reserves one source
 * for looping music, and preallocates a bounded one-shot source pool. No audio
 * allocation or file I/O occurs while playing a cue.
 *
 * This adapter owns the client's OpenAL context exclusively. Its lifecycle and
 * public playback methods must execute on the same main thread. A missing
 * device, malformed asset, or partial OpenAL initialization makes start()
 * return false after fully unwinding acquired resources.
 */
class OpenAlFrontendAudio final : public AudioPort {
public:
    static constexpr SoundHandle menu_confirm_sound{1U};
    static constexpr SoundHandle menu_back_sound{2U};
    static constexpr SoundHandle main_menu_music{3U};
    static constexpr SoundHandle menu_scroll_sound{4U};
    static constexpr SoundHandle tutorial_appear_sound{5U};
    static constexpr SoundHandle tutorial_disappear_sound{6U};
    static constexpr SoundHandle tutorial_music{7U};
    /** Recovered Training ambient bed: skybox Classic_B -> amb_rural. */
    static constexpr SoundHandle training_ambience{8U};
    /** Retail combat cues; names match the retail sound identifiers. */
    static constexpr SoundHandle pistol_shoot_sound{9U};
    static constexpr SoundHandle pistol_reload_sound{10U};
    static constexpr SoundHandle dig_hit_sound{11U};
    static constexpr SoundHandle block_build_sound{12U};
    static constexpr SoundHandle tool_switch_sound{13U};
    /** DIG_MISS_SOUND: every spade swing wooshes; hits add hitground. */
    static constexpr SoundHandle dig_swing_sound{14U};
    /** BULLET_HIT_SCENERY_SOUND recovered four-way random group. */
    static constexpr SoundHandle bullet_hit_sound_1{15U};
    static constexpr SoundHandle bullet_hit_sound_2{16U};
    static constexpr SoundHandle bullet_hit_sound_3{17U};
    static constexpr SoundHandle bullet_hit_sound_4{18U};
    /** FallingBlocks breakup/impact sample. */
    static constexpr SoundHandle block_debris_sound{19U};
    static constexpr SoundHandle pickaxe_hit_sound{20U};
    static constexpr SoundHandle super_spade_hit_sound{21U};
    static constexpr SoundHandle zombie_hit_sound{22U};
    static constexpr SoundHandle crowbar_damage_sound{23U};
    static constexpr SoundHandle crowbar_break_sound{24U};
    static constexpr SoundHandle knife_damage_sound{25U};
    static constexpr SoundHandle knife_break_sound{26U};
    static constexpr SoundHandle machete_damage_sound{27U};
    static constexpr SoundHandle machete_break_sound{28U};
    static constexpr SoundHandle riotstick_damage_sound{29U};
    static constexpr SoundHandle riotstick_break_sound{30U};
    static constexpr SoundHandle riotshield_damage_sound{31U};
    static constexpr SoundHandle riotshield_break_sound{32U};
    static constexpr SoundHandle explosion_sound{33U};
    static constexpr SoundHandle grenade_pin_sound{34U};
    static constexpr SoundHandle molotov_throw_sound{35U};
    static constexpr SoundHandle molotov_impact_sound{36U};

    /**
     * Movement foley. Ordered to match world::MovementSound so the frontend can
     * map a decision straight to a handle without a switch.
     *
     * Four interchangeable variants each for the two ground families; the
     * impacts ship one apiece.
     */
    static constexpr SoundHandle footstep_sound_1{37U};
    static constexpr SoundHandle footstep_sound_2{38U};
    static constexpr SoundHandle footstep_sound_3{39U};
    static constexpr SoundHandle footstep_sound_4{40U};
    static constexpr SoundHandle wade_sound_1{41U};
    static constexpr SoundHandle wade_sound_2{42U};
    static constexpr SoundHandle wade_sound_3{43U};
    static constexpr SoundHandle wade_sound_4{44U};
    static constexpr SoundHandle jump_sound{45U};
    static constexpr SoundHandle water_jump_sound{46U};
    static constexpr SoundHandle land_sound{47U};
    static constexpr SoundHandle water_land_sound{48U};
    static constexpr SoundHandle fall_hurt_sound{49U};
    /** Character.set_zoom transition cues recovered from the retail bank. */
    static constexpr SoundHandle zoom_in_sound{50U};
    static constexpr SoundHandle zoom_out_sound{51U};
    /** Local death/respawn countdown tones from Character.update_respawn_time. */
    static constexpr SoundHandle respawn_beep1_sound{52U};
    static constexpr SoundHandle respawn_beep2_sound{53U};

    explicit OpenAlFrontendAudio(OpenAlFrontendAudioConfig config = {});
    ~OpenAlFrontendAudio() override;

    OpenAlFrontendAudio(const OpenAlFrontendAudio&) = delete;
    OpenAlFrontendAudio& operator=(const OpenAlFrontendAudio&) = delete;
    OpenAlFrontendAudio(OpenAlFrontendAudio&&) = delete;
    OpenAlFrontendAudio& operator=(OpenAlFrontendAudio&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool start() override;
    [[nodiscard]] core::TickDecision tick(const core::TickContext& context) override;
    void stop() noexcept override;

    void set_listener(SoundPosition position) override;
    /**
     * Updates position and orientation atomically for correct left/right and
     *
     * front/rear panning. Vectors use canonical AoS world coordinates.
     */
    void set_listener_pose(SoundPosition position,
                           const std::array<float, 3U>& forward,
                           const std::array<float, 3U>& up);
    /** Install or clear the live VXL spatial-transmission callback. */
    void set_spatial_gain_resolver(void* context, SpatialGainResolver resolver) noexcept;
    void play_one_shot(SoundHandle sound, SoundPosition position, float gain) override;
    /** Plays an owner/UI cue at the listener without world-space stereo panning. */
    void play_head_relative_one_shot(SoundHandle sound, float gain = 1.0F);
    [[nodiscard]] SoundHandle preload_skin_sound(const std::filesystem::path& path);
    void play_skin_sound(SoundHandle sound,float gain,float pitch=1.0F);
    void stop_all() noexcept override;

    /** Play non-positional retail UI cues using the configured cue gain. */
    void play_menu_confirm();
    void play_menu_back();
    void play_menu_scroll();
    /** Replace the main menu bed with retail SECONDARY_MENU_MUSIC1. */
    [[nodiscard]] bool play_secondary_menu_music();
    /** Play zoom_in/zoom_out only on an actual toggle edge. */
    void play_zoom_toggle(bool enabled);
    /** Play one listener-local retail death countdown beat. */
    void play_respawn_countdown_beep(bool final_beat);
    /** Recovered retail help-panel cues (tutorial_app/tutorial_disapp). */
    void play_tutorial_appear();
    void play_tutorial_disappear();
    /** Loops tutorial_music_001 on the dedicated music source. */
    [[nodiscard]] bool play_tutorial_music();
    /** Loops the recovered amb_rural global bed for Training. */
    [[nodiscard]] bool play_training_ambience(float volume);
    void stop_ambience() noexcept;

    /** Deterministic selection from the recovered four-sample scenery group. */
    void play_bullet_impact(std::uint8_t variant, SoundPosition position, float gain = 1.0F);
    /** Play a recovered catalog group; missing/non-audible tools fail silently. */
    void play_weapon_shoot(std::uint8_t tool_id,
                           std::uint8_t variant,
                           SoundPosition position,
                           float gain = 1.0F,
                           bool head_relative = false,
                           SpatialSoundProfile profile = SpatialSoundProfile::ordinary,
                           std::string_view cosmetic_id = {});
    [[nodiscard]] bool has_cosmetic_fire(std::string_view cosmetic_id) const noexcept;
    [[nodiscard]] bool play_cosmetic_cue(std::string_view cosmetic_id,std::string_view cue,
        std::uint8_t variant,SoundPosition position,float gain=1.0F,bool head_relative=false);
    void play_weapon_reload(std::uint8_t tool_id,
                            std::uint8_t variant,
                            SoundPosition position,
                            float gain = 1.0F,
                            bool head_relative = false);
    void play_weapon_reload_done(std::uint8_t tool_id,
                                 std::uint8_t variant,
                                 SoundPosition position,
                                 float gain = 1.0F,
                                 bool head_relative = false);

    /**
     * Plays one recovered per-tool cue.
     *
     * Silently does nothing when retail has no cue in that role, which is the
     * parity-correct behaviour for the many deliberately silent tools.
     * `variant` selects within a numbered group such as `foo_001-004`.
     */
    void play_weapon_cue(std::uint8_t tool_id,
                         WeaponCue cue,
                         std::uint8_t variant,
                         SoundPosition position,
                         float gain = 1.0F,
                         bool head_relative = false,
                         SpatialSoundProfile profile = SpatialSoundProfile::ordinary);
    [[nodiscard]] bool has_weapon_cue(std::uint8_t tool_id, WeaponCue cue) const noexcept;

    /**
     * Starts a sustained looping cue and returns its handle.
     *
     * Retail's automatic weapons hold one infinite loop for as long as the
     * trigger is down and play a separate tail when it closes; firing a
     * one-shot per bullet instead machine-guns the sample. Returns
     * `invalid_loop_voice` when the tool has no cue in that role or every
     * loop source is busy.
     */
    [[nodiscard]] LoopVoice
    start_weapon_loop(std::uint8_t tool_id,
                      WeaponCue cue,
                      SoundPosition position,
                      float gain = 1.0F,
                      bool head_relative = false,
                      SpatialSoundProfile profile = SpatialSoundProfile::ordinary);
    /**
     * Start a sustained `sounds/<stem>.ogg` cue on the local presentation pool.
     *
     * This is deliberately separate from Protocol 168's byte-sized loop ids:
     * derived character effects such as jetpack thrust must never replace a
     * server-owned ambience loop that happens to use the same id.
     */
    [[nodiscard]] LoopVoice
    start_named_voice_loop(std::string_view stem,
                           SoundPosition position,
                           float gain = 1.0F,
                           bool head_relative = false,
                           SpatialSoundProfile profile = SpatialSoundProfile::ordinary);
    /** Repositions and retunes a live loop; call once per frame while held. */
    void update_weapon_loop(LoopVoice voice,
                            SoundPosition position,
                            float pitch = 1.0F,
                            float gain = 1.0F);
    /** Stops the loop. Pass a tail cue to play it as the loop closes. */
    void stop_weapon_loop(LoopVoice voice,
                          std::uint8_t tool_id,
                          WeaponCue tail,
                          SoundPosition position,
                          float gain = 1.0F,
                          bool head_relative = false,
                          SpatialSoundProfile profile = SpatialSoundProfile::ordinary);
    /**
     * Stops a loop with NO tail.
     *
     * Retail closes some loops silently -- the minigun's barrel spin authors no
     * stop sample at all, and every loop-owning weapon drops its loop without a
     * tail when it is unequipped. A separate overload rather than a sentinel cue,
     * because "no tail" is a real case and should read as one at the call site.
     */
    void stop_weapon_loop(LoopVoice voice) noexcept;
    /**
     * Plays a sound by asset stem, loading it on first use.
     *
     * The per-class voice banks reference several hundred distinct samples and
     * a session touches only the handful belonging to the classes actually in
     * play, so preloading them all would cost startup time and memory for
     * nothing. Handles are allocated lazily above the fixed range and cached, so
     * a repeated line loads once.
     *
     * Returns false when the stem does not resolve, so a caller can tell a
     * missing asset from a deliberate silence.
     *
     * `head_relative` pins the sound to the listener instead of placing it in
     * the world, so it plays at full volume regardless of distance. That is
     * how a shooter hears their OWN ordnance: a rocket lands 15-30 blocks out,
     * and at that range ordinary positional attenuation leaves almost nothing
     * audible even though the blast is the loudest thing in the game.
     */
    bool play_named_one_shot(std::string_view stem,
                             SoundPosition position,
                             float gain = 1.0F,
                             bool head_relative = false,
                             float start_offset = 0.0F,
                             float attenuation = 0.15F,
                             bool protect_voice = false,
                             float pitch = 1.0F);

    /** Start a server-selected `music/<stem>.ogg` track at its wire offset. */
    [[nodiscard]] bool play_named_music(std::string_view stem, float start_offset = 0.0F);
    /**
     * Decode and upload a map ambience without starting playback.
     *
     * OpenAL
     * buffer creation belongs to the audio owner thread, so the loading
     * gate calls this
     * before the first playable frame rather than letting
     * play_named_ambience perform cold
     * OGG I/O during world entry.
     */
    [[nodiscard]] bool preload_named_ambience(std::string_view stem);
    /**
     * Queue ambience decode on a worker; OpenAL upload is completed by tick().
     *

     * * Protocol 168 packet 22 uses this advance notice so packet 24 can begin
     * playback
     * without blocking the gameplay thread.
     */
    [[nodiscard]] bool preload_named_ambience_async(std::string_view stem);
    /** Replace the local map fallback with `ambients/<stem>.ogg`. */
    [[nodiscard]] bool
    play_named_ambience(std::string_view stem, float volume = 1.0F, float start_offset = 0.0F);
    [[nodiscard]] bool play_named_ambient_one_shot(std::string_view stem,
                                                   SoundPosition position,
                                                   float volume,
                                                   bool head_relative,
                                                   float attenuation = 0.15F,
                                                   float start_offset = 0.0F);

    /**
     * Own one Protocol 168 loop id on a bounded dedicated source.
     *
     *
     * `ambient_asset` selects the `ambients/` tree; false selects `sounds/`.
     * Positioned
     * loops remain allocated while distant but are hard-muted past
     * retail's 50-block hearing
     * radius, so approaching them works without
     * leaking quiet map-wide noise.
     */
    [[nodiscard]] bool start_named_loop(std::uint8_t loop_id,
                                        std::string_view stem,
                                        bool ambient_asset,
                                        SoundPosition position,
                                        float gain,
                                        bool head_relative,
                                        float attenuation = 0.15F,
                                        float start_offset = 0.0F);
    void update_named_loop(std::uint8_t loop_id, SoundPosition position);
    void stop_named_loop(std::uint8_t loop_id) noexcept;
    /** Stop every packet-owned loop without touching local weapon loops. */
    void stop_named_loops() noexcept;

    [[nodiscard]] std::size_t active_loop_voices() const noexcept;

    /**
     * Applies the retail Main-tab volume sliders immediately.
     *
     * Master volume scales every source through the listener; music volume
     * independently scales the looping music source. Values outside 0..1 or
     * calls from a non-owner thread fail closed and leave the prior gain.
     */
    [[nodiscard]] bool set_master_volume(float volume);
    [[nodiscard]] bool set_music_volume(float volume);
    [[nodiscard]] float master_volume() const noexcept;
    [[nodiscard]] float music_volume() const noexcept;

    /** Start/restart or stop the dedicated looping main-menu music source. */
    [[nodiscard]] bool start_menu_music();
    void stop_menu_music() noexcept;

    [[nodiscard]] bool is_started() const noexcept;
    [[nodiscard]] std::string_view playback_device() const noexcept;
    [[nodiscard]] std::size_t active_one_shot_voices() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::audio
