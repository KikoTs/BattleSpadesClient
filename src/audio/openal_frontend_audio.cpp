#include "battlespades/audio/openal_frontend_audio.hpp"
#include "battlespades/audio/server_audio_catalog.hpp"
#include "battlespades/audio/sound_groups.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_presentation.hpp"

#if defined(__APPLE__)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>
#endif

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4701)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace battlespades::audio {
namespace {

constexpr std::uintmax_t maximum_compressed_bytes{64U * 1024U * 1024U};
constexpr std::size_t maximum_decoded_bytes{256U * 1024U * 1024U};

struct DecodedAudio final {
    std::vector<short> samples;
    int channels{};
    int sample_rate{};
};

struct AsyncDecodedAudio final {
    DecodedAudio audio;
    std::string error;
    bool succeeded{};
};

using audio::sound_group_stems;

/**
 * AL_SOFT_direct_channels source property (alext.h). Retail audio.py:411-412
 * sets it on every source when the extension exists, so multichannel music
 * and stereo effects bypass virtual-speaker panning.
 */
#ifndef AL_DIRECT_CHANNELS_SOFT
#define AL_DIRECT_CHANNELS_SOFT 0x1033
#endif

/** ALC_SOFT_HRTF context attribute (alext.h). */
#ifndef ALC_HRTF_SOFT
#define ALC_HRTF_SOFT 0x1992
#endif

/** AL_REVERB_DECAY_TIME's legal range (efx.h). */
#if !defined(__APPLE__)
constexpr float minimum_reverb_decay{0.1F};
constexpr float maximum_reverb_decay{20.0F};
#endif

struct VorbisCloser final {
    void operator()(stb_vorbis* decoder) const noexcept {
        if (decoder != nullptr) {
            stb_vorbis_close(decoder);
        }
    }
};

using VorbisPtr = std::unique_ptr<stb_vorbis, VorbisCloser>;

[[nodiscard]] std::string vorbis_error_text(int error) {
    switch (error) {
    case VORBIS__no_error:
        return "no decoder error";
    case VORBIS_need_more_data:
        return "truncated OGG stream";
    case VORBIS_invalid_api_mixing:
        return "invalid decoder API use";
    case VORBIS_outofmem:
        return "decoder allocation failed";
    case VORBIS_feature_not_supported:
        return "unsupported Vorbis feature";
    case VORBIS_too_many_channels:
        return "too many Vorbis channels";
    case VORBIS_file_open_failure:
        return "OGG file open failure";
    case VORBIS_seek_without_length:
        return "Vorbis stream length unavailable";
    case VORBIS_unexpected_eof:
        return "unexpected end of OGG stream";
    case VORBIS_seek_invalid:
        return "invalid Vorbis seek data";
    case VORBIS_invalid_setup:
        return "invalid Vorbis setup";
    case VORBIS_invalid_stream:
        return "invalid Vorbis stream";
    case VORBIS_missing_capture_pattern:
        return "missing OGG capture pattern";
    case VORBIS_invalid_stream_structure_version:
        return "unsupported OGG stream version";
    case VORBIS_continued_packet_flag_invalid:
        return "invalid continued-packet flag";
    case VORBIS_incorrect_stream_serial_number:
        return "incorrect OGG stream serial number";
    case VORBIS_invalid_first_page:
        return "invalid first OGG page";
    case VORBIS_bad_packet_type:
        return "invalid Vorbis packet type";
    case VORBIS_cant_find_last_page:
        return "cannot locate final OGG page";
    case VORBIS_seek_failed:
        return "Vorbis seek failed";
    case VORBIS_ogg_skeleton_not_supported:
        return "OGG skeleton stream is unsupported";
    default:
        return "unknown Vorbis decoder error " + std::to_string(error);
    }
}

[[nodiscard]] std::string openal_error_text(ALenum error) {
    switch (error) {
    case AL_NO_ERROR:
        return "no OpenAL error";
    case AL_INVALID_NAME:
        return "invalid OpenAL object name";
    case AL_INVALID_ENUM:
        return "invalid OpenAL enum";
    case AL_INVALID_VALUE:
        return "invalid OpenAL value";
    case AL_INVALID_OPERATION:
        return "invalid OpenAL operation";
    case AL_OUT_OF_MEMORY:
        return "OpenAL out of memory";
    default:
        return "unknown OpenAL error " + std::to_string(error);
    }
}

void clear_openal_error() noexcept {
    while (alGetError() != AL_NO_ERROR) {
    }
}

[[nodiscard]] bool finite_position(SoundPosition position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

[[nodiscard]] bool valid_audio_stem(std::string_view stem) noexcept {
    if (stem.empty() || stem.size() > 96U) {
        return false;
    }
    return std::ranges::all_of(stem, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '_' || character == '-';
    });
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] bool
read_ogg(const std::filesystem::path& path, DecodedAudio& decoded, std::string& error) {
    static_assert(sizeof(short) == 2U, "stb_vorbis PCM output requires 16-bit short");

    std::error_code filesystem_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        error = "cannot stat audio asset '" + path_utf8(path) + "': " + filesystem_error.message();
        return false;
    }
    if (file_size == 0U || file_size > maximum_compressed_bytes ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<int>::max())) {
        error = "audio asset has invalid compressed size: '" + path_utf8(path) + "'";
        return false;
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        error = "cannot open audio asset '" + path_utf8(path) + "'";
        return false;
    }

    const auto byte_count = static_cast<std::size_t>(file_size);
    std::vector<unsigned char> encoded(byte_count);
    input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(byte_count));
    if (!input || input.gcount() != static_cast<std::streamsize>(byte_count)) {
        error = "short read from audio asset '" + path_utf8(path) + "'";
        return false;
    }

    int decoder_error{};
    VorbisPtr decoder{stb_vorbis_open_memory(
        encoded.data(), static_cast<int>(encoded.size()), &decoder_error, nullptr)};
    if (decoder == nullptr) {
        error = "cannot decode '" + path_utf8(path) + "': " + vorbis_error_text(decoder_error);
        return false;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(decoder.get());
    if ((info.channels != 1 && info.channels != 2) || info.sample_rate == 0U ||
        info.sample_rate > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        error = "unsupported channel count or sample rate in '" + path_utf8(path) + "'";
        return false;
    }

    const unsigned int frame_count = stb_vorbis_stream_length_in_samples(decoder.get());
    if (frame_count == 0U) {
        error = "empty or unseekable Vorbis stream: '" + path_utf8(path) + "'";
        return false;
    }

    const auto channels = static_cast<std::size_t>(info.channels);
    const auto frames = static_cast<std::size_t>(frame_count);
    if (frames > maximum_decoded_bytes / sizeof(short) / channels) {
        error = "decoded audio exceeds safety limit: '" + path_utf8(path) + "'";
        return false;
    }

    const std::size_t sample_count = frames * channels;
    if (sample_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "decoded audio exceeds OpenAL size limit: '" + path_utf8(path) + "'";
        return false;
    }

    decoded.samples.assign(sample_count, short{});
    std::size_t decoded_frames{};
    while (decoded_frames < frames) {
        const std::size_t sample_offset = decoded_frames * channels;
        const std::size_t remaining_samples = sample_count - sample_offset;
        const int result =
            stb_vorbis_get_samples_short_interleaved(decoder.get(),
                                                     info.channels,
                                                     decoded.samples.data() + sample_offset,
                                                     static_cast<int>(remaining_samples));
        if (result <= 0) {
            break;
        }
        decoded_frames += static_cast<std::size_t>(result);
    }

    if (decoded_frames == 0U) {
        error = "Vorbis stream produced no PCM samples: '" + path_utf8(path) + "'";
        return false;
    }
    const int decode_error = stb_vorbis_get_error(decoder.get());
    if (decode_error != VORBIS__no_error) {
        error = "Vorbis decoding failed for '" + path_utf8(path) +
                "': " + vorbis_error_text(decode_error);
        return false;
    }

    decoded.samples.resize(decoded_frames * channels);
    decoded.channels = info.channels;
    decoded.sample_rate = static_cast<int>(info.sample_rate);
    return true;
}

} // namespace

struct OpenAlFrontendAudio::Impl final {
    struct Buffer final {
        SoundHandle handle{};
        ALuint id{};
        float duration_seconds{};
        /** Decoded PCM size, for the music/ambience LRU budget. */
        std::size_t bytes{};
    };

    /** The three named asset trees; index into `named_cache`. */
    enum class NamedDirectory : std::uint8_t { sounds, ambients, music };

    [[nodiscard]] static std::optional<NamedDirectory>
    named_directory(std::string_view directory) noexcept {
        if (directory == "sounds") {
            return NamedDirectory::sounds;
        }
        if (directory == "ambients") {
            return NamedDirectory::ambients;
        }
        if (directory == "music") {
            return NamedDirectory::music;
        }
        return std::nullopt;
    }

    /**
     * One-shot requested while its sample was still decoding on a worker.
     * Played from tick() once uploaded, or dropped when it would be stale.
     */
    struct PendingOneShot final {
        std::string canonical;
        SoundPosition position{};
        float gain{1.0F};
        bool relative{};
        bool protect{};
        float start_offset{};
        float attenuation{retail_default_attenuation};
        SpatialSoundProfile profile{SpatialSoundProfile::ordinary};
        float pitch{1.0F};
        bool reverb_send{true};
        /** Character.play_vo owner (0 = none); see Voice::speaker. */
        std::uint16_t speaker{};
        std::chrono::steady_clock::time_point deadline{};
    };
    /** A first-use one-shot later than this is dropped rather than played late. */
    static constexpr std::chrono::milliseconds pending_one_shot_lifetime{300};
    static constexpr std::size_t maximum_pending_one_shots{64U};

    struct PendingAmbienceRequest final {
        std::string canonical;
        float volume{1.0F};
        float start_offset{};
    };

    /** A decoded music/ambience buffer that may be evicted when not playing. */
    struct EvictableBuffer final {
        std::string canonical;
        NamedDirectory directory{NamedDirectory::music};
        std::string stem;
        std::uint64_t last_used{};
    };
    /**
     * Decoded music + ambience PCM kept resident. A rotation touched ~530 MB
     * (12 tracks ~272 MB, 21 beds ~263 MB) that was never released; beyond
     * this budget the least-recently-used buffers not attached to any source
     * are deleted and re-decoded (off-thread) on next use.
     */
    static constexpr std::size_t evictable_pcm_budget_bytes{
        OpenAlFrontendAudio::music_ambience_pcm_budget_bytes};

    struct Voice final {
        ALuint source{};
        std::uint64_t sequence{};
        bool active{};
        /**
         * Protected from eviction while it is still playing.
         *
         * Voice lines run for seconds where a cue runs for a fraction of one, so
         * oldest-first stealing picks the spawn line every time and cuts it off
         * mid-word the moment a gun fires.
         */
        bool protected_voice{};
        /**
         * Character.play_vo (character.pyd 0x10024030): each character owns
         * ONE voice line. A new line that actually starts closes the
         * previous one (stop_current_vo), so a character's jump, land,
         * fall-hurt, spawn and death lines never stack. 0 = not a VO line.
         */
        std::uint16_t speaker{};
    };

    /**
     * A loop whose lifetime is owned by Protocol 168's sound packets.
     *
     * These
     * sources are separate from local weapon loops so a map ambience
     * emitter cannot evict a
     * minigun or other held-tool sound.
     */
    struct ServerLoopSlot final {
        ALuint source{};
        std::uint8_t loop_id{};
        SoundPosition position{};
        float requested_gain{};
        bool relative{};
        bool active{};
    };

    struct PendingNamedBuffer final {
        SoundHandle handle{};
        std::filesystem::path path;
        NamedDirectory directory{NamedDirectory::sounds};
        std::string stem;
        std::future<AsyncDecodedAudio> decode;
    };

    struct NamedBufferRequest final {
        SoundHandle handle{};
        std::string canonical;
        bool pending{};
    };

    struct PendingMusicRequest final {
        std::string canonical;
        float start_offset{};
        /** fade_speed_when_finished as seconds (1.5 for the select bed). */
        float fade_seconds{retail_music_fade_seconds};
    };

    struct PendingServerLoopRequest final {
        std::string canonical;
        SoundPosition position{};
        float gain{};
        bool relative{};
        float attenuation{};
        float start_offset{};
    };

    explicit Impl(OpenAlFrontendAudioConfig requested_config)
        : config{std::move(requested_config)} {}

    [[nodiscard]] bool on_owner_thread() const noexcept {
        return owner_thread == std::this_thread::get_id();
    }

    /** A HUD_AUDIO_ZONE cue: 2D at the listener, no reverb send. */
    void play_hud(SoundHandle handle, float gain) {
        play(handle,
             {},
             gain,
             true,
             false,
             0.0F,
             retail_default_attenuation,
             SpatialSoundProfile::ordinary,
             1.0F,
             false);
    }

    /**
     * Retail never occludes audio (the only audio raycast is the reverb
     * probe), so the source gain is simply the requested volume. OpenAL Soft
     * clamps the attenuated result to AL_MAX_GAIN (1), exactly as retail's
     * volume-4 crate chute cue relies on.
     */
    [[nodiscard]] static float resolved_spatial_gain(float requested_gain) noexcept {
        return std::clamp(requested_gain, 0.0F, 4.0F);
    }

    /** Retail GameSound.set_position offset; head-relative sources stay at 0. */
    [[nodiscard]] static SoundPosition source_position(SoundPosition nominal,
                                                       bool relative) noexcept {
        return relative ? SoundPosition{} : retail_source_position(nominal);
    }

    /** audio.py:411-412: AL_DIRECT_CHANNELS_SOFT on every source. */
    void apply_direct_channels(ALuint source) const noexcept {
        if (direct_channels_supported && source != 0U) {
            alSourcei(source, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);
        }
    }

    [[nodiscard]] ALuint buffer_for(SoundHandle handle) const noexcept {
        const auto found =
            std::find_if(buffers.begin(), buffers.end(), [handle](const Buffer& item) {
                return item.handle == handle;
            });
        return found == buffers.end() ? 0U : found->id;
    }

    [[nodiscard]] float duration_for(SoundHandle handle) const noexcept {
        const auto found =
            std::find_if(buffers.begin(), buffers.end(), [handle](const Buffer& item) {
                return item.handle == handle;
            });
        return found == buffers.end() ? 0.0F : found->duration_seconds;
    }

    [[nodiscard]] bool
    upload_buffer(SoundHandle handle, const std::filesystem::path& path, DecodedAudio decoded) {
        ALuint buffer{};
        clear_openal_error();
        alGenBuffers(1, &buffer);
        ALenum error = alGetError();
        if (error != AL_NO_ERROR || buffer == 0U) {
            last_error = "cannot create OpenAL buffer for '" + path_utf8(path) +
                         "': " + openal_error_text(error);
            return false;
        }

        const ALenum format = decoded.channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
        const std::size_t byte_count = decoded.samples.size() * sizeof(short);
        alBufferData(buffer,
                     format,
                     decoded.samples.data(),
                     static_cast<ALsizei>(byte_count),
                     static_cast<ALsizei>(decoded.sample_rate));
        error = alGetError();
        if (error != AL_NO_ERROR) {
            alDeleteBuffers(1, &buffer);
            last_error = "cannot upload OpenAL buffer for '" + path_utf8(path) +
                         "': " + openal_error_text(error);
            return false;
        }

        const auto frames = decoded.samples.size() / static_cast<std::size_t>(decoded.channels);
        const float duration = static_cast<float>(frames) / static_cast<float>(decoded.sample_rate);
        buffers.push_back(Buffer{
            .handle = handle, .id = buffer, .duration_seconds = duration, .bytes = byte_count});
        return true;
    }

    /** Cached handle for a named asset: no path build, no filesystem call. */
    [[nodiscard]] SoundHandle find_named(NamedDirectory directory, std::string_view stem) {
        auto& cache = named_cache[static_cast<std::size_t>(directory)];
        const auto found = cache.find(stem);
        if (found == cache.end()) {
            return 0U;
        }
        if (const auto evictable = evictable_buffers.find(found->second);
            evictable != evictable_buffers.end()) {
            evictable->second.last_used = ++evictable_clock;
        }
        return found->second;
    }

    /** Records a resident named buffer and applies the music/ambience budget. */
    void remember_named(NamedDirectory directory,
                        std::string_view stem,
                        const std::string& canonical,
                        SoundHandle handle) {
        named_cache[static_cast<std::size_t>(directory)].insert_or_assign(std::string{stem},
                                                                          handle);
        optional_sound_cache.insert_or_assign(canonical, handle);
        if (directory != NamedDirectory::sounds) {
            evictable_buffers.insert_or_assign(
                handle,
                EvictableBuffer{canonical, directory, std::string{stem}, ++evictable_clock});
            evict_to_budget();
        }
    }

    /** True while any owned source still references the OpenAL buffer. */
    [[nodiscard]] bool buffer_attached(ALuint id) const noexcept {
        const auto attached = [id](ALuint source) {
            if (source == 0U) {
                return false;
            }
            ALint current{};
            alGetSourcei(source, AL_BUFFER, &current);
            return static_cast<ALuint>(current) == id;
        };
        if (attached(music_source) || attached(fade_source) || attached(ambience_source)) {
            return true;
        }
        return std::ranges::any_of(voices, [&](const Voice& v) { return attached(v.source); }) ||
               std::ranges::any_of(loop_slots,
                                   [&](const LoopSlot& s) { return attached(s.source); }) ||
               std::ranges::any_of(server_loop_slots,
                                   [&](const ServerLoopSlot& s) { return attached(s.source); });
    }

    /** Resident decoded music + ambience PCM, in bytes. */
    [[nodiscard]] std::size_t evictable_bytes() const noexcept {
        std::size_t total{};
        for (const auto& [handle, entry] : evictable_buffers) {
            static_cast<void>(entry);
            const auto found = std::ranges::find(buffers, handle, &Buffer::handle);
            if (found != buffers.end()) {
                total += found->bytes;
            }
        }
        return total;
    }

    /**
     * Deletes least-recently-used music/ambience buffers that no source holds
     * until the resident PCM fits the budget. A playing bed or track is never
     * touched; an evicted asset is simply decoded again on its next use.
     */
    void evict_to_budget() {
        auto total = evictable_bytes();
        while (total > evictable_pcm_budget_bytes) {
            auto victim = evictable_buffers.end();
            for (auto entry = evictable_buffers.begin(); entry != evictable_buffers.end();
                 ++entry) {
                if (entry->first == music_track) {
                    continue;
                }
                const ALuint id = buffer_for(entry->first);
                if (id != 0U && buffer_attached(id)) {
                    continue;
                }
                if (victim == evictable_buffers.end() ||
                    entry->second.last_used < victim->second.last_used) {
                    victim = entry;
                }
            }
            if (victim == evictable_buffers.end()) {
                return;
            }
            const SoundHandle handle = victim->first;
            if (const auto found = std::ranges::find(buffers, handle, &Buffer::handle);
                found != buffers.end()) {
                total -= std::min(total, found->bytes);
                if (found->id != 0U) {
                    alDeleteBuffers(1, &found->id);
                }
                buffers.erase(found);
            }
            named_cache[static_cast<std::size_t>(victim->second.directory)].erase(
                victim->second.stem);
            if (const auto cached = optional_sound_cache.find(victim->second.canonical);
                cached != optional_sound_cache.end() && cached->second == handle) {
                optional_sound_cache.erase(cached);
            }
            evictable_buffers.erase(victim);
        }
    }

    [[nodiscard]] std::filesystem::path named_path(std::string_view directory,
                                                   std::string_view stem) const {
        return (config.asset_root / std::string{directory} / (std::string{stem} + ".ogg"))
            .lexically_normal();
    }

    /**
     * Plays a named one-shot now, or queues it for the tick that uploads its
     * worker-decoded sample (first use only; dropped if later than
     * pending_one_shot_lifetime). Returns false only for a missing asset.
     */
    [[nodiscard]] bool play_named_or_defer(std::string_view directory,
                                           std::string_view stem,
                                           PendingOneShot shot) {
        const auto request = request_named_buffer_async(directory, stem);
        if (request.handle != 0U) {
            play(request.handle,
                 shot.position,
                 shot.gain,
                 shot.relative,
                 shot.protect,
                 shot.start_offset,
                 shot.attenuation,
                 shot.profile,
                 shot.pitch,
                 shot.reverb_send,
                 shot.speaker);
            return true;
        }
        if (!request.pending) {
            return false;
        }
        if (pending_one_shots.size() >= maximum_pending_one_shots) {
            pending_one_shots.erase(pending_one_shots.begin());
        }
        shot.canonical = request.canonical;
        shot.deadline = std::chrono::steady_clock::now() + pending_one_shot_lifetime;
        pending_one_shots.push_back(std::move(shot));
        return true;
    }

    [[nodiscard]] bool load_buffer(SoundHandle handle, const std::filesystem::path& path) {
        DecodedAudio decoded;
        if (!read_ogg(path, decoded, last_error)) {
            return false;
        }
        return upload_buffer(handle, path, std::move(decoded));
    }

    /**
     * Named assets (sounds/, ambients/, music/) resolve through the cache
     * below before any path is built or the filesystem is touched; a cold
     * asset decodes on a worker and every caller either defers (one-shots,
     * beds, music, server loops) or reserves its loop slot until tick()
     * uploads it. Nothing named decodes Vorbis on the game thread.
     *
     * Begin file I/O and Vorbis decoding away from the OpenAL owner thread.
     *
     *
     * Only the final alBufferData upload is pumped by tick(). Long server
     * music tracks
     * therefore cannot freeze the first playable frame.
     */
    [[nodiscard]] NamedBufferRequest request_named_buffer_async(std::string_view directory,
                                                                std::string_view stem) {
        const auto tree = named_directory(directory);
        if (!valid_audio_stem(stem) || !tree.has_value()) {
            last_error = "unsafe asynchronous audio resource";
            return {};
        }
        if (const SoundHandle cached = find_named(*tree, stem); cached != 0U) {
            // Hit: no canonical string or filesystem call is needed.
            return {.handle = cached, .canonical = {}, .pending = false};
        }
        const auto path = named_path(directory, stem);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            last_error = "named audio resource is unavailable: " + path_utf8(path);
            return {};
        }
        const auto canonical = path_utf8(path);
        if (const auto found = optional_sound_cache.find(canonical);
            found != optional_sound_cache.end()) {
            remember_named(*tree, stem, canonical, found->second);
            return {.handle = found->second, .canonical = canonical, .pending = false};
        }
        if (pending_named_buffers.contains(canonical)) {
            return {.canonical = canonical, .pending = true};
        }
        const SoundHandle handle = next_optional_handle++;
        PendingNamedBuffer pending;
        pending.handle = handle;
        pending.path = path;
        pending.directory = *tree;
        pending.stem = std::string{stem};
        pending.decode = std::async(std::launch::async, [path]() {
            AsyncDecodedAudio result;
            try {
                result.succeeded = read_ogg(path, result.audio, result.error);
            } catch (const std::exception& exception) {
                result.error = std::string{"audio decode worker failed: "} + exception.what();
            } catch (...) {
                result.error = "audio decode worker failed with an unknown exception";
            }
            return result;
        });
        pending_named_buffers.emplace(canonical, std::move(pending));
        return {.canonical = canonical, .pending = true};
    }

    void pump_async_named_buffers() {
        for (auto iterator = pending_named_buffers.begin();
             iterator != pending_named_buffers.end();) {
            if (iterator->second.decode.wait_for(std::chrono::seconds{0}) !=
                std::future_status::ready) {
                ++iterator;
                continue;
            }
            auto result = iterator->second.decode.get();
            const auto canonical = iterator->first;
            const auto handle = iterator->second.handle;
            const auto path = iterator->second.path;
            const auto directory = iterator->second.directory;
            const auto stem = iterator->second.stem;
            bool uploaded{};
            if (result.succeeded) {
                uploaded = upload_buffer(handle, path, std::move(result.audio));
            } else {
                last_error = std::move(result.error);
            }
            iterator = pending_named_buffers.erase(iterator);
            if (uploaded) {
                remember_named(directory, stem, canonical, handle);
            }

            if (pending_ambience.has_value() && pending_ambience->canonical == canonical) {
                const auto request = *pending_ambience;
                pending_ambience.reset();
                if (uploaded) {
                    static_cast<void>(
                        play_ambience(handle, request.volume, request.start_offset));
                }
            }
            const auto now = std::chrono::steady_clock::now();
            for (auto shot = pending_one_shots.begin(); shot != pending_one_shots.end();) {
                if (shot->canonical != canonical) {
                    ++shot;
                    continue;
                }
                const auto request = *shot;
                shot = pending_one_shots.erase(shot);
                if (uploaded && now <= request.deadline) {
                    play(handle,
                         request.position,
                         request.gain,
                         request.relative,
                         request.protect,
                         request.start_offset,
                         request.attenuation,
                         request.profile,
                         request.pitch,
                         request.reverb_send,
                         request.speaker);
                }
            }
            for (auto& slot : loop_slots) {
                if (!slot.active || !slot.pending || slot.pending_canonical != canonical) {
                    continue;
                }
                slot.pending = false;
                slot.pending_canonical.clear();
                const ALuint buffer = uploaded ? buffer_for(handle) : 0U;
                if (buffer == 0U || !begin_loop_playback(slot, buffer, slot.reverb_send)) {
                    slot.active = false;
                    slot.voice = invalid_loop_voice;
                }
            }

            if (pending_music.has_value() && pending_music->canonical == canonical) {
                const auto request = *pending_music;
                pending_music.reset();
                if (uploaded) {
                    static_cast<void>(
                        play_music(handle, request.start_offset, request.fade_seconds));
                }
            }
            for (auto loop = pending_server_loops.begin(); loop != pending_server_loops.end();) {
                if (loop->second.canonical != canonical) {
                    ++loop;
                    continue;
                }
                const auto loop_id = loop->first;
                const auto request = loop->second;
                loop = pending_server_loops.erase(loop);
                if (uploaded) {
                    static_cast<void>(start_server_loop(loop_id,
                                                        handle,
                                                        request.position,
                                                        request.gain,
                                                        request.relative,
                                                        request.attenuation,
                                                        request.start_offset));
                }
            }
        }
    }

    void
    apply_start_offset(ALuint source, SoundHandle handle, float requested_offset) const noexcept {
        if (!std::isfinite(requested_offset) || requested_offset <= 0.0F) {
            return;
        }
        const float duration = duration_for(handle);
        if (duration <= 0.0F) {
            return;
        }
        const float offset = std::fmod(requested_offset, duration);
        if (offset > 0.0F) {
            alSourcef(source, AL_SEC_OFFSET, offset);
        }
    }

    [[nodiscard]] std::vector<SoundHandle>
    load_optional_group(std::string_view group, const std::filesystem::path& sound_root) {
        std::vector<SoundHandle> handles;
        for (const auto& stem : sound_group_stems(group)) {
            const auto path = sound_root / (stem + ".ogg");
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) {
                continue;
            }
            const auto canonical = path_utf8(path.lexically_normal());
            if (const auto found = optional_sound_cache.find(canonical);
                found != optional_sound_cache.end()) {
                handles.push_back(found->second);
                continue;
            }
            const SoundHandle handle = next_optional_handle++;
            if (load_buffer(handle, path)) {
                optional_sound_cache.emplace(canonical, handle);
                handles.push_back(handle);
            }
        }
        return handles;
    }

    [[nodiscard]] bool create_sources() {
        clear_openal_error();
        alGenSources(1, &music_source);
        ALenum error = alGetError();
        if (error != AL_NO_ERROR || music_source == 0U) {
            last_error = "cannot allocate OpenAL music source: " + openal_error_text(error);
            return false;
        }

        alGenSources(1, &fade_source);
        error = alGetError();
        if (error != AL_NO_ERROR || fade_source == 0U) {
            last_error = "cannot allocate OpenAL music fade source: " + openal_error_text(error);
            return false;
        }

        alGenSources(1, &ambience_source);
        error = alGetError();
        if (error != AL_NO_ERROR || ambience_source == 0U) {
            last_error = "cannot allocate OpenAL ambience source: " + openal_error_text(error);
            return false;
        }

        for (auto& slot : loop_slots) {
            alGenSources(1, &slot.source);
            error = alGetError();
            if (error != AL_NO_ERROR || slot.source == 0U) {
                last_error = "cannot allocate OpenAL loop source: " + openal_error_text(error);
                return false;
            }
            slot.active = false;
            slot.voice = invalid_loop_voice;
        }
        for (auto& slot : server_loop_slots) {
            alGenSources(1, &slot.source);
            error = alGetError();
            if (error != AL_NO_ERROR || slot.source == 0U) {
                last_error =
                    "cannot allocate Protocol 168 loop source: " + openal_error_text(error);
                return false;
            }
        }

        voices.reserve(config.max_one_shot_voices);
        for (std::size_t index{}; index < config.max_one_shot_voices; ++index) {
            ALuint source{};
            alGenSources(1, &source);
            error = alGetError();
            if (error != AL_NO_ERROR || source == 0U) {
                // Decision D5: retail had 128 OpenAL sources. A device that
                // cannot provide them all still gets the 64-voice floor, with
                // protected stingers and voice lines surviving voice theft.
                if (voices.size() >= minimum_one_shot_voices) {
                    clear_openal_error();
                    break;
                }
                last_error =
                    "cannot allocate bounded OpenAL voice pool: " + openal_error_text(error);
                return false;
            }
            voices.push_back(Voice{.source = source});
        }
        apply_direct_channels(music_source);
        apply_direct_channels(fade_source);
        apply_direct_channels(ambience_source);
        for (const auto& slot : loop_slots) {
            apply_direct_channels(slot.source);
        }
        for (const auto& slot : server_loop_slots) {
            apply_direct_channels(slot.source);
        }
        for (const auto& voice : voices) {
            apply_direct_channels(voice.source);
        }
        clear_openal_error();
        return true;
    }

    /**
     * Restore retail's in-world reverb send when OpenAL Soft exposes EFX.
     *
     * EFX
     * is optional on every platform. Failure therefore leaves the dry
     * mixer operational
     * instead of making audio startup (or the game) fail.
     */
    void initialise_world_reverb() noexcept {
#if defined(__APPLE__)
        // Apple's native OpenAL framework does not expose OpenAL Soft's EFX
        // header or extension entry points. Reverb is optional; dry positional
        // audio remains fully operational on macOS.
        return;
#else
        if (device == nullptr || alcIsExtensionPresent(device, "ALC_EXT_EFX") == ALC_FALSE) {
            return;
        }
        gen_effects = reinterpret_cast<LPALGENEFFECTS>(alGetProcAddress("alGenEffects"));
        delete_effects = reinterpret_cast<LPALDELETEEFFECTS>(alGetProcAddress("alDeleteEffects"));
        effect_i = reinterpret_cast<LPALEFFECTI>(alGetProcAddress("alEffecti"));
        effect_f = reinterpret_cast<LPALEFFECTF>(alGetProcAddress("alEffectf"));
        gen_effect_slots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(
            alGetProcAddress("alGenAuxiliaryEffectSlots"));
        delete_effect_slots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(
            alGetProcAddress("alDeleteAuxiliaryEffectSlots"));
        effect_slot_i =
            reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(alGetProcAddress("alAuxiliaryEffectSloti"));
        if (gen_effects == nullptr || delete_effects == nullptr || effect_i == nullptr ||
            effect_f == nullptr || gen_effect_slots == nullptr || delete_effect_slots == nullptr ||
            effect_slot_i == nullptr) {
            return;
        }

        clear_openal_error();
        gen_effects(1, &world_reverb_effect);
        if (world_reverb_effect == 0U || alGetError() != AL_NO_ERROR) {
            world_reverb_effect = 0U;
            return;
        }
        effect_i(world_reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        // GameScene.update_audio_effects overwrites set_global_reverb's
        // defaults every update; the scene starts dry (outdoor targets).
        effect_f(world_reverb_effect, AL_REVERB_GAIN, environment.reverb_amount);
        effect_f(world_reverb_effect,
                 AL_REVERB_DECAY_TIME,
                 std::clamp(environment.reverb_size, minimum_reverb_decay, maximum_reverb_decay));
        effect_f(world_reverb_effect, AL_REVERB_GAINHF, retail_reverb_gain_hf);
        applied_reverb_gain = environment.reverb_amount;
        applied_reverb_decay = environment.reverb_size;
        gen_effect_slots(1, &world_reverb_slot);
        if (world_reverb_slot != 0U) {
            effect_slot_i(
                world_reverb_slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(world_reverb_effect));
        }
        if (world_reverb_slot == 0U || alGetError() != AL_NO_ERROR) {
            if (world_reverb_slot != 0U) {
                delete_effect_slots(1, &world_reverb_slot);
                world_reverb_slot = 0U;
            }
            delete_effects(1, &world_reverb_effect);
            world_reverb_effect = 0U;
            clear_openal_error();
        }
#endif
    }

    /**
     * Release the optional EFX objects while this instance's context is current.
     *
     * The terrain gain resolver is runtime policy, not an OpenAL resource owner.
     * Keeping teardown here prevents changing/refreshing occlusion from silently
     * deleting the reverb bus used by every positional retail sound.
     */
    void release_world_reverb() noexcept {
#if !defined(__APPLE__)
        if (world_reverb_slot != 0U && delete_effect_slots != nullptr) {
            delete_effect_slots(1, &world_reverb_slot);
            world_reverb_slot = 0U;
        }
        if (world_reverb_effect != 0U && delete_effects != nullptr) {
            delete_effects(1, &world_reverb_effect);
            world_reverb_effect = 0U;
        }
#endif
    }

    /**
     * Route one source by retail audio zone, not by head-relativity.
     *
     * media.play sends only IN_WORLD_AUDIO_ZONE players to the reverb slot.
     * That includes the local player's 2D weapon, foley, VO and zoom cues;
     * server PlaySound/PlayAmbientSound (HUD_AUDIO_ZONE), menu and HUD cues
     * stay dry.
     */
    void apply_world_reverb(ALuint source, bool reverb_send) const noexcept {
#if defined(__APPLE__)
        static_cast<void>(source);
        static_cast<void>(reverb_send);
#else
        if (source == 0U || world_reverb_slot == 0U) {
            return;
        }
        alSource3i(source,
                   AL_AUXILIARY_SEND_FILTER,
                   reverb_send ? static_cast<ALint>(world_reverb_slot) : AL_EFFECTSLOT_NULL,
                   0,
                   AL_FILTER_NULL);
#endif
    }

    /**
     * Write the smoothed room to the EFX effect and re-bind the slot: OpenAL
     * Soft only picks up effect parameter changes on AL_EFFECTSLOT_EFFECT.
     */
    void update_world_reverb() noexcept {
#if !defined(__APPLE__)
        if (world_reverb_effect == 0U || world_reverb_slot == 0U || effect_f == nullptr ||
            effect_slot_i == nullptr) {
            return;
        }
        constexpr float epsilon{1.0e-4F};
        if (std::fabs(environment.reverb_amount - applied_reverb_gain) < epsilon &&
            std::fabs(environment.reverb_size - applied_reverb_decay) < epsilon) {
            return;
        }
        effect_f(world_reverb_effect,
                 AL_REVERB_GAIN,
                 std::clamp(environment.reverb_amount, 0.0F, 1.0F));
        effect_f(world_reverb_effect,
                 AL_REVERB_DECAY_TIME,
                 std::clamp(environment.reverb_size, minimum_reverb_decay, maximum_reverb_decay));
        effect_f(world_reverb_effect, AL_REVERB_GAINHF, retail_reverb_gain_hf);
        effect_slot_i(
            world_reverb_slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(world_reverb_effect));
        applied_reverb_gain = environment.reverb_amount;
        applied_reverb_decay = environment.reverb_size;
#endif
    }

    [[nodiscard]] bool play_music(SoundHandle track,
                                  float start_offset = 0.0F,
                                  float fade_seconds_when_finished = retail_music_fade_seconds) {
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return false;
        }

        const ALuint buffer = buffer_for(track);
        if (buffer == 0U || music_source == 0U) {
            last_error = "music buffer or source is unavailable for track " + std::to_string(track);
            return false;
        }

        clear_openal_error();
        // Retail dedupe: a replay of the already-playing track is a no-op.
        if (music_requested && music_track == track) {
            ALint state{};
            alGetSourcei(music_source, AL_SOURCE_STATE, &state);
            if (state == AL_PLAYING) {
                return true;
            }
        }
        // media.play_music: a still-fading old track is zeroed first, then
        // stop_music hands the current track to the fade source, which fades
        // at that track's own fade_speed_when_finished (1/6.5 by default).
        if (fade_active && fade_source != 0U) {
            alSourceStop(fade_source);
            alSourcei(fade_source, AL_BUFFER, 0);
            fade_active = false;
        }
        if (music_requested) {
            begin_music_fade();
        }
        alSourceStop(music_source);
        alSourcei(music_source, AL_BUFFER, 0);
        alSourcei(music_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(music_source, AL_LOOPING, AL_TRUE);
        alSourcef(music_source, AL_ROLLOFF_FACTOR, 0.0F);
        alSourcef(music_source, AL_GAIN, config.music_gain);
        alSource3f(music_source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcei(music_source, AL_BUFFER, static_cast<ALint>(buffer));
        apply_start_offset(music_source, track, start_offset);
        alSourcePlay(music_source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            last_error = "cannot start looping music track: " + openal_error_text(error);
            music_requested = false;
            return false;
        }

        music_requested = true;
        music_track = track;
        music_fade_seconds = fade_seconds_when_finished > 0.0F &&
                                     std::isfinite(fade_seconds_when_finished)
                                 ? fade_seconds_when_finished
                                 : retail_music_fade_seconds;
        return true;
    }

    /** `media.play_music(name, fade_speed_when_finished=1/fade_seconds)`. */
    [[nodiscard]] bool
    play_named_music(std::string_view stem, float start_offset, float fade_seconds) {
        if (context == nullptr || !on_owner_thread()) {
            return false;
        }
        const auto request = request_named_buffer_async("music", stem);
        if (request.handle != 0U) {
            pending_music.reset();
            const bool started = play_music(request.handle, start_offset, fade_seconds);
            if (started) {
                named_music_stem = std::string{stem};
            }
            return started;
        }
        if (request.pending) {
            // Last packet wins, matching the synchronous source replacement path.
            pending_music = PendingMusicRequest{request.canonical, start_offset, fade_seconds};
            named_music_stem = std::string{stem};
            return true;
        }
        return false;
    }

    /**
     * Move a playing music source onto the fade source (retail old_music).
     * Returns false when nothing audible was playing.
     */
    bool begin_music_fade() noexcept {
        if (music_source == 0U || fade_source == 0U || context == nullptr) {
            return false;
        }
        ALint state{};
        alGetSourcei(music_source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            return false;
        }
        alSourceStop(fade_source);
        alSourcei(fade_source, AL_BUFFER, 0);
        std::swap(music_source, fade_source);
        ALfloat gain{};
        alGetSourcef(fade_source, AL_GAIN, &gain);
        fade_gain = gain;
        fade_rate = 1.0F / music_fade_seconds;
        fade_active = true;
        return true;
    }

    /**
     * media.stop_music(): the current track fades out rather than cutting;
     * `instant` is the teardown path (stop(), stop_all()).
     */
    void stop_music(bool instant = false) noexcept {
        const bool was_requested = music_requested;
        music_requested = false;
        named_music_stem.clear();
        if (!instant && was_requested && context != nullptr) {
            if (fade_active && fade_source != 0U) {
                // A second stop drops the older fading track, as retail's
                // old_music reference is replaced.
                alSourceStop(fade_source);
                alSourcei(fade_source, AL_BUFFER, 0);
                fade_active = false;
            }
            if (begin_music_fade()) {
                music_fade_seconds = retail_music_fade_seconds;
                return;
            }
        }
        if (music_source != 0U && context != nullptr) {
            alSourceStop(music_source);
            alSourcei(music_source, AL_BUFFER, 0);
        }
        if (instant && fade_active && fade_source != 0U && context != nullptr) {
            alSourceStop(fade_source);
            alSourcei(fade_source, AL_BUFFER, 0);
            fade_active = false;
        }
        music_fade_seconds = retail_music_fade_seconds;
    }

    /** Global ambient bed: a dedicated looping non-positional source. */
    [[nodiscard]] bool play_ambience(SoundHandle bed, float gain, float start_offset = 0.0F) {
        // Any explicit bed supersedes a bed still decoding on a worker.
        pending_ambience.reset();
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return false;
        }
        const ALuint buffer = buffer_for(bed);
        if (buffer == 0U || ambience_source == 0U) {
            last_error = "ambience buffer or source is unavailable for bed " + std::to_string(bed);
            return false;
        }

        clear_openal_error();
        alSourceStop(ambience_source);
        alSourcei(ambience_source, AL_BUFFER, 0);
        alSourcei(ambience_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(ambience_source, AL_LOOPING, AL_TRUE);
        alSourcef(ambience_source, AL_ROLLOFF_FACTOR, 0.0F);
        // Retail starts a bed at full volume; AMBIENCE_FADE_AMOUNT is the
        // indoor-ducking smoothing factor, not a fade-in.
        ambience_target_gain = std::clamp(gain, 0.0F, 4.0F);
        ambience_gain = ducked_ambience_volume(ambience_target_gain, environment.ambience_ducking);
        alSourcef(ambience_source, AL_GAIN, ambience_gain);
        apply_world_reverb(ambience_source, false);
        alSource3f(ambience_source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcei(ambience_source, AL_BUFFER, static_cast<ALint>(buffer));
        apply_start_offset(ambience_source, bed, start_offset);
        alSourcePlay(ambience_source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            last_error = "cannot start looping ambience: " + openal_error_text(error);
            ambience_active = false;
            return false;
        }
        ambience_active = true;
        return true;
    }

    void stop_ambience() noexcept {
        ambience_active = false;
        pending_ambience.reset();
        if (ambience_source != 0U && context != nullptr) {
            alSourceStop(ambience_source);
            alSourcei(ambience_source, AL_BUFFER, 0);
        }
    }

    [[nodiscard]] ServerLoopSlot* find_server_loop(std::uint8_t loop_id) noexcept {
        for (auto& slot : server_loop_slots) {
            if (slot.active && slot.loop_id == loop_id) {
                return &slot;
            }
        }
        return nullptr;
    }

    /** Retail culls only at allocation; a playing loop is never re-gated. */
    void refresh_server_loop_gain(ServerLoopSlot& slot) noexcept {
        alSourcef(slot.source, AL_GAIN, resolved_spatial_gain(slot.requested_gain));
    }

    void stop_server_loop(ServerLoopSlot& slot) noexcept {
        if (slot.source != 0U && context != nullptr) {
            alSourceStop(slot.source);
            alSourcei(slot.source, AL_BUFFER, 0);
        }
        slot.active = false;
        slot.loop_id = 0U;
        slot.position = {};
        slot.requested_gain = 0.0F;
        slot.relative = false;
    }

    [[nodiscard]] bool start_server_loop(std::uint8_t loop_id,
                                         SoundHandle handle,
                                         SoundPosition position,
                                         float gain,
                                         bool relative,
                                         float attenuation,
                                         float start_offset) {
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return false;
        }
        if (!finite_position(position) || !std::isfinite(gain) || !std::isfinite(attenuation) ||
            !std::isfinite(start_offset) || gain < 0.0F || attenuation < 0.0F) {
            last_error = "invalid Protocol 168 loop parameters";
            return false;
        }
        const ALuint buffer = buffer_for(handle);
        if (buffer == 0U) {
            last_error = "Protocol 168 loop buffer is unavailable";
            return false;
        }
        // MediaManager.play rejects a positioned stream beyond HEARING_DISTANCE
        // before it allocates; once started a loop is never re-culled.
        if (!relative && !within_retail_hearing_distance(listener_position, position)) {
            if (ServerLoopSlot* replaced = find_server_loop(loop_id); replaced != nullptr) {
                stop_server_loop(*replaced);
            }
            return true;
        }
        ServerLoopSlot* slot = find_server_loop(loop_id);
        if (slot == nullptr) {
            const auto available =
                std::ranges::find(server_loop_slots, false, &ServerLoopSlot::active);
            if (available == server_loop_slots.end()) {
                last_error = "Protocol 168 loop source pool is full";
                return false;
            }
            slot = &*available;
        } else {
            stop_server_loop(*slot);
        }

        clear_openal_error();
        slot->active = true;
        slot->loop_id = loop_id;
        slot->position = position;
        slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
        slot->relative = relative;
        alSourceStop(slot->source);
        alSourcei(slot->source, AL_BUFFER, 0);
        alSourcei(slot->source, AL_LOOPING, AL_TRUE);
        alSourcei(slot->source, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
        alSourcef(slot->source, AL_PITCH, 1.0F);
        alSourcef(slot->source, AL_ROLLOFF_FACTOR, relative ? 0.0F : attenuation);
        alSourcef(slot->source, AL_REFERENCE_DISTANCE, retail_reference_distance);
        const auto placed = source_position(position, relative);
        alSource3f(slot->source, AL_POSITION, placed.x, placed.y, placed.z);
        alSource3f(slot->source, AL_VELOCITY, 0.0F, 0.0F, 0.0F);
        // PlaySound(23)/PlayAmbientSound(24) use HUD_AUDIO_ZONE: always dry.
        apply_world_reverb(slot->source, false);
        alSourcei(slot->source, AL_BUFFER, static_cast<ALint>(buffer));
        refresh_server_loop_gain(*slot);
        apply_start_offset(slot->source, handle, start_offset);
        alSourcePlay(slot->source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            stop_server_loop(*slot);
            last_error = "cannot start Protocol 168 loop: " + openal_error_text(error);
            return false;
        }
        return true;
    }

    void reclaim_finished_voices() noexcept {
        for (Voice& voice : voices) {
            if (!voice.active) {
                continue;
            }
            ALint state{};
            alGetSourcei(voice.source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED || state == AL_INITIAL) {
                alSourcei(voice.source, AL_BUFFER, 0);
                voice.active = false;
                voice.protected_voice = false;
                voice.speaker = 0U;
            }
        }
    }

    [[nodiscard]] Voice* acquire_voice() noexcept {
        reclaim_finished_voices();
        const auto available = std::find_if(
            voices.begin(), voices.end(), [](const Voice& voice) { return !voice.active; });
        if (available != voices.end()) {
            return &*available;
        }

        // UI feedback must remain immediate under a cue flood. Steal only the
        // oldest one-shot; the dedicated music source is never affected.
        //
        // Protected voices are skipped: a spoken line is long and would always
        // be the oldest, so plain oldest-first stealing silences exactly the
        // sound the player most wanted to hear. If EVERY voice is protected we
        // fall back to stealing the oldest anyway rather than dropping the new
        // cue, because going deaf is worse than clipping one line.
        const auto oldest_unprotected = std::min_element(
            voices.begin(), voices.end(), [](const Voice& left, const Voice& right) {
                if (left.protected_voice != right.protected_voice) {
                    return !left.protected_voice;
                }
                return left.sequence < right.sequence;
            });
        return oldest_unprotected == voices.end() ? nullptr : &*oldest_unprotected;
    }

    void play(SoundHandle handle,
              SoundPosition position,
              float gain,
              bool relative,
              bool protect = false,
              float start_offset = 0.0F,
              float attenuation = retail_default_attenuation,
              SpatialSoundProfile profile = SpatialSoundProfile::ordinary,
              float pitch = 1.0F,
              bool reverb_send = true,
              std::uint16_t speaker = 0U) {
        static_cast<void>(profile);
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return;
        }
        if (!finite_position(position) || !std::isfinite(gain) || !std::isfinite(start_offset) ||
            !std::isfinite(attenuation) || !std::isfinite(pitch) || gain < 0.0F ||
            attenuation < 0.0F || pitch <= 0.0F) {
            last_error = "invalid OpenAL one-shot position or gain";
            return;
        }
        // Retail rejects the cue before allocating a player. AL_MAX_DISTANCE
        // only clamps attenuation and otherwise leaks every distant dig/build
        // event into the listener at a permanent low volume.
        if (!relative && !within_retail_hearing_distance(listener_position, position)) {
            return;
        }

        const ALuint buffer = buffer_for(handle);
        if (buffer == 0U || handle == OpenAlFrontendAudio::main_menu_music ||
            handle == OpenAlFrontendAudio::tutorial_music ||
            handle == OpenAlFrontendAudio::training_ambience) {
            last_error = "unknown or non-one-shot frontend sound handle " + std::to_string(handle);
            return;
        }

        Voice* const voice = acquire_voice();
        if (voice == nullptr) {
            last_error = "OpenAL one-shot voice pool is empty";
            return;
        }

        clear_openal_error();
        voice->protected_voice = protect;
        alSourceStop(voice->source);
        alSourcei(voice->source, AL_BUFFER, 0);
        alSourcei(voice->source, AL_LOOPING, AL_FALSE);
        alSourcei(voice->source, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
        alSourcef(voice->source, AL_PITCH, pitch);
        alSourcef(voice->source, AL_GAIN, resolved_spatial_gain(gain));
        alSourcef(voice->source, AL_ROLLOFF_FACTOR, relative ? 0.0F : attenuation);
        // Retail leaves OpenAL's reference distance (1) and max distance
        // (FLT_MAX); the 50-block pre-cull above is its only range limit.
        alSourcef(voice->source, AL_REFERENCE_DISTANCE, retail_reference_distance);
        const auto placed = source_position(position, relative);
        alSource3f(voice->source, AL_POSITION, placed.x, placed.y, placed.z);
        alSource3f(voice->source, AL_VELOCITY, 0.0F, 0.0F, 0.0F);
        apply_world_reverb(voice->source, reverb_send);
        alSourcei(voice->source, AL_BUFFER, static_cast<ALint>(buffer));
        apply_start_offset(voice->source, handle, start_offset);
        alSourcePlay(voice->source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            voice->active = false;
            last_error = "cannot play OpenAL one-shot: " + openal_error_text(error);
            return;
        }

        ++next_sequence;
        voice->sequence = next_sequence;
        voice->active = true;
        voice->speaker = speaker;
        if (speaker != 0U) {
            // stop_current_vo runs only after the new line started.
            for (Voice& other : voices) {
                if (&other != voice && other.active && other.speaker == speaker) {
                    alSourceStop(other.source);
                    alSourcei(other.source, AL_BUFFER, 0);
                    other.active = false;
                    other.protected_voice = false;
                    other.speaker = 0U;
                }
            }
        }
    }

    OpenAlFrontendAudioConfig config;
    std::string playback_device_name;
    ALCdevice* device{};
    ALCcontext* context{};
    ALuint music_source{};
    ALuint fade_source{};
    ALuint ambience_source{};
#if !defined(__APPLE__)
    ALuint world_reverb_effect{};
    ALuint world_reverb_slot{};
    LPALGENEFFECTS gen_effects{};
    LPALDELETEEFFECTS delete_effects{};
    LPALEFFECTI effect_i{};
    LPALEFFECTF effect_f{};
    LPALGENAUXILIARYEFFECTSLOTS gen_effect_slots{};
    LPALDELETEAUXILIARYEFFECTSLOTS delete_effect_slots{};
    LPALAUXILIARYEFFECTSLOTI effect_slot_i{};
#endif
    float fade_gain{};
    /** Outgoing track's fade speed in volume per second (old_music_fade_speed). */
    float fade_rate{1.0F / retail_music_fade_seconds};
    /** Current track's fade_speed_when_finished, as seconds from full volume. */
    float music_fade_seconds{retail_music_fade_seconds};
    bool fade_active{};
    float ambience_gain{};
    float ambience_target_gain{};
    bool ambience_active{};
    /** Last GameScene.update_audio_effects state applied to EFX and the bed. */
    EnvironmentAudioState environment{};
    float applied_reverb_gain{};
    float applied_reverb_decay{1.0F};
    bool direct_channels_supported{};
    std::vector<Buffer> buffers;
    std::vector<Voice> voices;
    std::string last_error;
    std::thread::id owner_thread{};
    std::uint64_t next_sequence{};
    std::uint64_t next_pitch_sequence{};
    SoundPosition listener_position{};
    float master_volume{1.0F};
    bool music_requested{};
    SoundHandle music_track{OpenAlFrontendAudio::main_menu_music};
    static constexpr std::size_t tool_count{65U};
    struct CosmeticFire final { std::vector<SoundHandle> samples; float gain{1.0F}; };
    std::map<std::string, CosmeticFire, std::less<>> cosmetic_fire_groups;
    std::map<std::string,std::map<std::string,std::vector<SoundHandle>,std::less<>>,std::less<>> cosmetic_cues;
    std::array<std::vector<SoundHandle>, tool_count> weapon_shoot_groups;
    std::array<std::vector<SoundHandle>, tool_count> weapon_reload_groups;
    std::array<std::vector<SoundHandle>, tool_count> weapon_reload_done_groups;
    using CueGroups =
        std::array<std::vector<SoundHandle>, static_cast<std::size_t>(WeaponCue::count)>;
    std::array<CueGroups, tool_count> weapon_sound_sets;
    /** Semitone bounds of each bound cue row (retail cue-list slots 3/4). */
    using CuePitches = std::array<PitchBounds, static_cast<std::size_t>(WeaponCue::count)>;
    std::array<CuePitches, tool_count> weapon_cue_pitches{};
    std::map<std::string, SoundHandle, std::less<>> optional_sound_cache;
    /** Per named tree (sounds/ambients/music): stem -> resident handle. */
    std::array<std::map<std::string, SoundHandle, std::less<>>, 3U> named_cache;
    std::map<SoundHandle, EvictableBuffer> evictable_buffers;
    std::uint64_t evictable_clock{};
    std::vector<PendingOneShot> pending_one_shots;
    std::optional<PendingAmbienceRequest> pending_ambience;
    std::map<std::string, PendingNamedBuffer, std::less<>> pending_named_buffers;
    std::optional<PendingMusicRequest> pending_music;
    /** Stem of the last play_named_music track; cleared by any other music. */
    std::string named_music_stem;
    std::map<std::uint8_t, PendingServerLoopRequest> pending_server_loops;
    SoundHandle next_optional_handle{1'000U};

    /**
     * Sustained loop sources, held apart from the one-shot pool.
     *
     * Four is enough for every simultaneous retail loop a single player can
     * drive (held trigger, minigun spin, a tool loop and one projectile).
     */
    // Automatic weapons, minigun spin and nearby replicated jetpacks can all
    // coexist. Four sources made later effects silently disappear in a normal
    // 12-player match; this remains a fixed, bounded pool.
    static constexpr std::size_t loop_source_count{24U};
    struct LoopSlot final {
        ALuint source{};
        LoopVoice voice{};
        SoundPosition position{};
        float requested_gain{};
        SpatialSoundProfile profile{SpatialSoundProfile::ordinary};
        bool relative{};
        bool active{};
        /**
         * Reserved for a first-use named loop whose sample is still decoding
         * on a worker; tick() starts it at the latest position and gain.
         */
        bool pending{};
        bool reverb_send{true};
        std::string pending_canonical;
    };
    std::array<LoopSlot, loop_source_count> loop_slots{};

    /** Configures and starts a reserved loop slot on `buffer`. */
    [[nodiscard]] bool begin_loop_playback(LoopSlot& slot, ALuint buffer, bool reverb_send) {
        clear_openal_error();
        alSourceStop(slot.source);
        alSourcei(slot.source, AL_BUFFER, 0);
        alSourcei(slot.source, AL_LOOPING, AL_TRUE);
        alSourcei(slot.source, AL_SOURCE_RELATIVE, slot.relative ? AL_TRUE : AL_FALSE);
        alSourcef(slot.source, AL_PITCH, 1.0F);
        alSourcef(slot.source,
                  AL_ROLLOFF_FACTOR,
                  slot.relative ? 0.0F : spatial_rolloff(slot.profile));
        alSourcef(slot.source, AL_GAIN, resolved_spatial_gain(slot.requested_gain));
        alSourcef(slot.source, AL_REFERENCE_DISTANCE, retail_reference_distance);
        const auto placed = source_position(slot.position, slot.relative);
        alSource3f(slot.source, AL_POSITION, placed.x, placed.y, placed.z);
        apply_world_reverb(slot.source, reverb_send);
        alSourcei(slot.source, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcePlay(slot.source);
        return alGetError() == AL_NO_ERROR;
    }
    LoopVoice next_loop_voice{1U};

    /**
     * Packet-owned loops are isolated from weapon loops.
     *
     * A map may carry a
     * global bed plus several local emitters while the local
     * player simultaneously owns a
     * firing and spin loop. Sharing four weapon
     * sources made ambience disappear under combat
     * load.
     */
    static constexpr std::size_t server_loop_source_count{16U};
    std::array<ServerLoopSlot, server_loop_source_count> server_loop_slots{};

    /** Retail culls only at allocation; a started loop keeps its volume. */
    void refresh_weapon_loop_gain(LoopSlot& slot) noexcept {
        if (!slot.active) {
            return;
        }
        alSourcef(slot.source, AL_GAIN, resolved_spatial_gain(slot.requested_gain));
    }

    [[nodiscard]] LoopSlot* find_loop(LoopVoice voice) noexcept {
        if (voice == invalid_loop_voice) {
            return nullptr;
        }
        for (auto& slot : loop_slots) {
            if (slot.active && slot.voice == voice) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<SoundHandle>* cue_group(std::uint8_t tool_id,
                                                            WeaponCue cue) const noexcept {
        const auto index = static_cast<std::size_t>(cue);
        if (tool_id >= weapon_sound_sets.size() ||
            index >= static_cast<std::size_t>(WeaponCue::count)) {
            return nullptr;
        }
        const auto& group = weapon_sound_sets[tool_id][index];
        return group.empty() ? nullptr : &group;
    }
};

bool valid_openal_frontend_audio_config(const OpenAlFrontendAudioConfig& config) noexcept {
    constexpr std::size_t maximum_voice_count{maximum_one_shot_voices};
    return !config.asset_root.empty() && config.max_one_shot_voices > 0U &&
           config.max_one_shot_voices <= maximum_voice_count && std::isfinite(config.music_gain) &&
           config.music_gain >= 0.0F && config.music_gain <= 4.0F &&
           std::isfinite(config.cue_gain) && config.cue_gain >= 0.0F && config.cue_gain <= 4.0F &&
           config.playback_device.size() <= 512U &&
           std::ranges::none_of(config.playback_device,
                               [](unsigned char byte) { return byte < 0x20U || byte == 0x7FU; });
}

OpenAlFrontendAudio::OpenAlFrontendAudio(OpenAlFrontendAudioConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

OpenAlFrontendAudio::~OpenAlFrontendAudio() {
    stop();
}

std::string_view OpenAlFrontendAudio::name() const noexcept {
    return "openal-frontend-audio";
}

bool OpenAlFrontendAudio::start() {
    if (impl_->device != nullptr || impl_->context != nullptr) {
        impl_->last_error = "OpenAL frontend audio is already started";
        return false;
    }
    if (!valid_openal_frontend_audio_config(impl_->config)) {
        impl_->last_error = "invalid OpenAL frontend audio configuration";
        return false;
    }
    if (alcGetCurrentContext() != nullptr) {
        impl_->last_error = "another OpenAL context is already current on the main thread";
        return false;
    }

    try {
        impl_->last_error.clear();
        impl_->owner_thread = std::this_thread::get_id();
        // Opening a WASAPI endpoint alone does not establish that it can play:
        // disconnected Bluetooth outputs can open but reject their own mix
        // format at alcCreateContext. Only automatic device selection may try
        // other outputs; an explicit choice must never redirect private audio.
        std::vector<std::string> candidates;
        if (!impl_->config.playback_device.empty())
            candidates.push_back(impl_->config.playback_device);
        else
            candidates.emplace_back();
        const auto all_devices = alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") != ALC_FALSE;
        const auto specifier = all_devices
                                   ? alcGetEnumValue(nullptr, "ALC_ALL_DEVICES_SPECIFIER")
                                   : ALC_DEVICE_SPECIFIER;
        if (impl_->config.playback_device.empty() &&
            (all_devices || alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT") != ALC_FALSE)) {
            const auto* names = alcGetString(nullptr, specifier);
            for (std::size_t index{}; names != nullptr && *names != '\0' && index < 32U; ++index) {
                const std::string name{names};
                if (std::ranges::find(candidates, name) == candidates.end()) candidates.push_back(name);
                names += name.size() + 1U;
            }
        }
        std::vector<std::string> attempted;
        for (const auto& candidate : candidates) {
            impl_->device = alcOpenDevice(candidate.empty() ? nullptr : candidate.c_str());
            if (impl_->device == nullptr) continue;
            const auto* label = alcGetString(impl_->device, specifier);
            const std::string resolved = label != nullptr ? label : candidate;
            if (std::ranges::find(attempted, resolved) == attempted.end()) {
                attempted.push_back(resolved);
                // Retail shipped OpenAL Soft 1.13 ("1.1 ALSOFT 1.13" in its
                // OpenAL32.dll), which predates HRTF. Modern OpenAL Soft
                // turns HRTF on by itself for headphone outputs, which
                // re-colours and re-levels every cue; request it off.
                const std::array<ALCint, 3U> context_attributes{
                    ALC_HRTF_SOFT, retail_context_hrtf ? ALC_TRUE : ALC_FALSE, 0};
                const bool hrtf_extension =
                    alcIsExtensionPresent(impl_->device, "ALC_SOFT_HRTF") != ALC_FALSE;
                impl_->context = alcCreateContext(
                    impl_->device, hrtf_extension ? context_attributes.data() : nullptr);
            }
            if (impl_->context != nullptr) {
                impl_->playback_device_name = resolved;
                if (attempted.size() > 1U || (!impl_->config.playback_device.empty() &&
                                            candidate != impl_->config.playback_device))
                    std::fprintf(stderr, "[audio] Using available playback device: %s\n", resolved.c_str());
                break;
            }
            static_cast<void>(alcCloseDevice(impl_->device));
            impl_->device = nullptr;
        }
        if (impl_->context == nullptr) {
            impl_->last_error = impl_->config.playback_device.empty()
                                    ? "No available audio output accepted playback; reconnect your audio device and restart the game"
                                    : "Selected audio output could not start: " + impl_->config.playback_device +
                                          "; reconnect this device and restart the game";
            stop();
            return false;
        }
        if (alcMakeContextCurrent(impl_->context) == ALC_FALSE) {
            impl_->last_error = "OpenAL Soft could not make its playback context current";
            stop();
            return false;
        }

        clear_openal_error();
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        alListener3f(AL_POSITION, 0.0F, 0.0F, 0.0F);
        alListener3f(AL_VELOCITY, 0.0F, 0.0F, 0.0F);
        constexpr std::array<ALfloat, 6U> initial_orientation{-1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F};
        alListenerfv(AL_ORIENTATION, initial_orientation.data());
        alListenerf(AL_GAIN, impl_->master_volume);
        const ALenum listener_error = alGetError();
        if (listener_error != AL_NO_ERROR) {
            impl_->last_error =
                "cannot configure OpenAL listener: " + openal_error_text(listener_error);
            stop();
            return false;
        }

        // Reserving all fixed caches before creating OpenAL objects ensures an
        // allocation exception cannot orphan a generated buffer or source.
        impl_->buffers.reserve(512U);
        impl_->direct_channels_supported =
            alIsExtensionPresent("AL_SOFT_direct_channels") != AL_FALSE;
        const auto& root = impl_->config.asset_root;
        if (!impl_->load_buffer(main_menu_music, root / "music" / "mainmenu.ogg") ||
            !impl_->load_buffer(menu_confirm_sound, root / "sounds" / "menu_confirmA.ogg") ||
            !impl_->load_buffer(menu_back_sound, root / "sounds" / "menu_backA.ogg") ||
            !impl_->load_buffer(menu_scroll_sound, root / "sounds" / "menu_scrollA.ogg") ||
            !impl_->load_buffer(tutorial_appear_sound, root / "sounds" / "tutorial_app.ogg") ||
            !impl_->load_buffer(tutorial_disappear_sound,
                                root / "sounds" / "tutorial_disapp.ogg") ||
            !impl_->load_buffer(tutorial_music, root / "music" / "tutorial_music_001.ogg") ||
            !impl_->load_buffer(training_ambience, root / "ambients" / "amb_rural.ogg") ||
            !impl_->load_buffer(pistol_shoot_sound, root / "sounds" / "pistolshoot.ogg") ||
            !impl_->load_buffer(pistol_reload_sound, root / "sounds" / "pistolreload.ogg") ||
            !impl_->load_buffer(dig_hit_sound, root / "sounds" / "hitground.ogg") ||
            !impl_->load_buffer(block_build_sound, root / "sounds" / "build.ogg") ||
            !impl_->load_buffer(tool_switch_sound, root / "sounds" / "switch.ogg") ||
            !impl_->load_buffer(dig_swing_sound, root / "sounds" / "woosh.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_1, root / "sounds" / "bullet_hit_001.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_2, root / "sounds" / "bullet_hit_002.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_3, root / "sounds" / "bullet_hit_003.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_4, root / "sounds" / "bullet_hit_004.ogg") ||
            !impl_->load_buffer(block_debris_sound, root / "sounds" / "block_debris.ogg") ||
            !impl_->load_buffer(pickaxe_hit_sound, root / "sounds" / "hitground_pickaxe.ogg") ||
            !impl_->load_buffer(super_spade_hit_sound, root / "sounds" / "hitground_super.ogg") ||
            !impl_->load_buffer(zombie_hit_sound, root / "sounds" / "hitground_zombie.ogg") ||
            !impl_->load_buffer(crowbar_damage_sound,
                                root / "sounds" / "hitground_crowbar_damage.ogg") ||
            !impl_->load_buffer(crowbar_break_sound,
                                root / "sounds" / "hitground_crowbar_break.ogg") ||
            !impl_->load_buffer(knife_damage_sound,
                                root / "sounds" / "hitground_knife_damage.ogg") ||
            !impl_->load_buffer(knife_break_sound, root / "sounds" / "hitground_knife_break.ogg") ||
            !impl_->load_buffer(machete_damage_sound,
                                root / "sounds" / "hitground_machete_damage.ogg") ||
            !impl_->load_buffer(machete_break_sound,
                                root / "sounds" / "hitground_machete_break.ogg") ||
            !impl_->load_buffer(riotstick_damage_sound,
                                root / "sounds" / "hitground_riotstick_damage.ogg") ||
            !impl_->load_buffer(riotstick_break_sound,
                                root / "sounds" / "hitground_riotstick_break.ogg") ||
            !impl_->load_buffer(riotshield_damage_sound,
                                root / "sounds" / "hitground_riotshield_damage.ogg") ||
            !impl_->load_buffer(riotshield_break_sound,
                                root / "sounds" / "hitground_riotshield_break.ogg") ||
            !impl_->load_buffer(explosion_sound, root / "sounds" / "explode.ogg") ||
            !impl_->load_buffer(grenade_pin_sound, root / "sounds" / "pin.ogg") ||
            !impl_->load_buffer(molotov_throw_sound, root / "sounds" / "molotov_throw.ogg") ||
            !impl_->load_buffer(molotov_impact_sound,
                                root / "sounds" / "molotov_land_explode.ogg") ||
            // Movement foley. Every stem below was confirmed present on disk.
            !impl_->load_buffer(footstep_sound_1, root / "sounds" / "footstep_001.ogg") ||
            !impl_->load_buffer(footstep_sound_2, root / "sounds" / "footstep_002.ogg") ||
            !impl_->load_buffer(footstep_sound_3, root / "sounds" / "footstep_003.ogg") ||
            !impl_->load_buffer(footstep_sound_4, root / "sounds" / "footstep_004.ogg") ||
            !impl_->load_buffer(wade_sound_1, root / "sounds" / "wade_001.ogg") ||
            !impl_->load_buffer(wade_sound_2, root / "sounds" / "wade_002.ogg") ||
            !impl_->load_buffer(wade_sound_3, root / "sounds" / "wade_003.ogg") ||
            !impl_->load_buffer(wade_sound_4, root / "sounds" / "wade_004.ogg") ||
            !impl_->load_buffer(jump_sound, root / "sounds" / "jump.ogg") ||
            !impl_->load_buffer(water_jump_sound, root / "sounds" / "waterjump.ogg") ||
            !impl_->load_buffer(land_sound, root / "sounds" / "land.ogg") ||
            !impl_->load_buffer(water_land_sound, root / "sounds" / "waterland.ogg") ||
            !impl_->load_buffer(fall_hurt_sound, root / "sounds" / "fallhurt.ogg") ||
            !impl_->load_buffer(zoom_in_sound, root / "sounds" / "zoom_in.ogg") ||
            !impl_->load_buffer(zoom_out_sound, root / "sounds" / "zoom_out.ogg") ||
            !impl_->load_buffer(respawn_beep1_sound, root / "sounds" / "beep1.ogg") ||
            !impl_->load_buffer(respawn_beep2_sound, root / "sounds" / "beep2.ogg") ||
            !impl_->create_sources()) {
            stop();
            return false;
        }
        impl_->initialise_world_reverb();

        // The generated 65-tool table is the common audio contract for the
        // debug lab and future Protocol 168 sessions. Optional groups are
        // predecoded here so a shot never performs gameplay-thread file I/O.
        for (const auto& weapon : world::weapon_catalog()) {
            impl_->weapon_shoot_groups[weapon.tool_id] =
                impl_->load_optional_group(weapon.shoot_sound, root / "sounds");
            impl_->weapon_reload_groups[weapon.tool_id] =
                impl_->load_optional_group(weapon.reload_sound, root / "sounds");
            impl_->weapon_reload_done_groups[weapon.tool_id] =
                impl_->load_optional_group(weapon.reload_done_sound, root / "sounds");
            // Retail keeps these in method bodies rather than class
            // attributes; the generator recovers them into WeaponSoundSet.
            // An empty role means retail is genuinely silent there.
            auto& set = impl_->weapon_sound_sets[weapon.tool_id];
            auto& pitches = impl_->weapon_cue_pitches[weapon.tool_id];
            const auto bind = [&](WeaponCue cue, std::string_view group) {
                set[static_cast<std::size_t>(cue)] =
                    impl_->load_optional_group(group, root / "sounds");
                pitches[static_cast<std::size_t>(cue)] =
                    retail_cue_group_pitch(group, cue == WeaponCue::throw_release);
            };
            bind(WeaponCue::fire_loop, weapon.sounds.fire_loop);
            bind(WeaponCue::fire_tail, weapon.sounds.fire_tail);
            bind(WeaponCue::spin_loop, weapon.sounds.spin_loop);
            bind(WeaponCue::melee_miss, weapon.sounds.melee_miss);
            bind(WeaponCue::melee_hit_block, weapon.sounds.melee_hit_block);
            bind(WeaponCue::melee_hit_player, weapon.sounds.melee_hit_player);
            bind(WeaponCue::empty_fire, weapon.sounds.empty_fire);
            bind(WeaponCue::pin, weapon.sounds.pin);
            bind(WeaponCue::throw_release, weapon.sounds.throw_release);
            bind(WeaponCue::tool_loop_start, weapon.sounds.tool_loop_start);
            bind(WeaponCue::tool_loop, weapon.sounds.tool_loop);
            bind(WeaponCue::tool_loop_stop, weapon.sounds.tool_loop_stop);
            bind(WeaponCue::tool_extra, weapon.sounds.tool_extra);
        }

        for (const auto& item : world::load_weapon_presentations(root)) {
            Impl::CosmeticFire fire;
            fire.gain = item.fire_gain;
            for (const auto& stem : item.fire_samples) {
                const auto group = impl_->load_optional_group(stem, root.parent_path()/"client/cosmetics/sounds");
                fire.samples.insert(fire.samples.end(), group.begin(), group.end());
            }
            if (!fire.samples.empty()) impl_->cosmetic_fire_groups.emplace(item.id, std::move(fire));
            for(const auto& [cue,stems]:item.cues)for(const auto& stem:stems){
                const auto group=impl_->load_optional_group(stem,root.parent_path()/"client/cosmetics/sounds");
                auto& samples=impl_->cosmetic_cues[item.id][cue];samples.insert(samples.end(),group.begin(),group.end());
            }
        }
        if (impl_->config.play_music_on_start && !impl_->play_music(main_menu_music)) {
            stop();
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        try {
            impl_->last_error =
                "OpenAL frontend startup exception: " + std::string{exception.what()};
        } catch (...) {
        }
        stop();
        return false;
    } catch (...) {
        try {
            impl_->last_error = "unknown OpenAL frontend startup exception";
        } catch (...) {
        }
        stop();
        return false;
    }
}

void OpenAlFrontendAudio::play_bullet_impact(std::uint8_t variant,
                                             SoundPosition position,
                                             float gain) {
    constexpr std::array<SoundHandle, 4U> samples{
        bullet_hit_sound_1, bullet_hit_sound_2, bullet_hit_sound_3, bullet_hit_sound_4};
    // weapons/__init__.py:64: play_pitched(BULLET_HIT_SCENERY_SOUND, 1.0, pos),
    // +-1.2 semitones.
    ++impl_->next_pitch_sequence;
    const auto draw = static_cast<std::uint32_t>(
        (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) >> 16U) ^ variant;
    impl_->play(samples[variant % samples.size()],
                position,
                gain,
                false,
                false,
                0.0F,
                retail_default_attenuation,
                SpatialSoundProfile::ordinary,
                retail_sound_pitch_ratio(
                    retail_bullet_hit_pitch.minimum, retail_bullet_hit_pitch.maximum, draw));
}

bool OpenAlFrontendAudio::has_cosmetic_fire(std::string_view id) const noexcept {
    return impl_->cosmetic_fire_groups.contains(id);
}
bool OpenAlFrontendAudio::play_cosmetic_cue(std::string_view id,std::string_view cue,std::uint8_t variant,SoundPosition position,float gain,bool relative){
    const auto item=impl_->cosmetic_cues.find(id);if(item==impl_->cosmetic_cues.end())return false;
    const auto group=item->second.find(cue);if(group==item->second.end()||group->second.empty())return false;
    // Cosmetic packs replace Tool.play_sound cues: the local player's are dry.
    impl_->play(group->second[variant%group->second.size()],position,gain,relative,false,0.0F,
                retail_default_attenuation,SpatialSoundProfile::ordinary,1.0F,
                tool_cue_reverb_send(relative));return true;
}

void OpenAlFrontendAudio::play_weapon_shoot(std::uint8_t tool_id,
                                            std::uint8_t variant,
                                            SoundPosition position,
                                            float gain,
                                            bool head_relative,
                                            SpatialSoundProfile profile,
                                            std::string_view cosmetic_id) {
    if (tool_id >= impl_->weapon_shoot_groups.size()) {
        return;
    }
    const auto cosmetic = impl_->cosmetic_fire_groups.find(cosmetic_id);
    const auto& group = cosmetic != impl_->cosmetic_fire_groups.end()
        ? cosmetic->second.samples : impl_->weapon_shoot_groups[tool_id];
    if (cosmetic != impl_->cosmetic_fire_groups.end()) gain *= cosmetic->second.gain;
    if (!group.empty()) {
        const auto* weapon = world::find_weapon_definition(tool_id);
        const auto pitch_bounds =
            weapon == nullptr ? std::array<float, 2U>{} : weapon->shoot_sound_pitch;
        ++impl_->next_pitch_sequence;
        const auto pitch_draw = static_cast<std::uint32_t>(
            (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
        impl_->play(group[variant % group.size()],
                    head_relative ? SoundPosition{} : position,
                    gain,
                    head_relative,
                    false,
                    0.0F,
                    spatial_rolloff(profile),
                    profile,
                    retail_sound_pitch_ratio(pitch_bounds[0U], pitch_bounds[1U], pitch_draw),
                    tool_cue_reverb_send(head_relative));
    }
}

void OpenAlFrontendAudio::play_double_shotgun_barrel(bool second_barrel,
                                                     SoundPosition position,
                                                     float gain,
                                                     bool head_relative,
                                                     SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    // SHOTGUN2_SHOOT_SOUND / SHOTGUN2_SECOND_SHOOT_SOUND: both +-0.8 semitones.
    ++impl_->next_pitch_sequence;
    const auto pitch_draw =
        static_cast<std::uint32_t>(impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL);
    Impl::PendingOneShot shot;
    shot.position = head_relative ? SoundPosition{} : position;
    shot.gain = gain;
    shot.relative = head_relative;
    shot.attenuation = spatial_rolloff(profile);
    shot.profile = profile;
    shot.pitch = retail_sound_pitch_ratio(-0.8F, 0.8F, pitch_draw);
    shot.reverb_send = tool_cue_reverb_send(head_relative);
    static_cast<void>(impl_->play_named_or_defer(
        "sounds", second_barrel ? "shotgun_double_fire02" : "shotgun_double_fire01",
        std::move(shot)));
}

void OpenAlFrontendAudio::play_weapon_reload(std::uint8_t tool_id,
                                             std::uint8_t variant,
                                             SoundPosition position,
                                             float gain,
                                             bool head_relative) {
    if (tool_id >= impl_->weapon_reload_groups.size()) {
        return;
    }
    const auto& group = impl_->weapon_reload_groups[tool_id];
    if (!group.empty()) {
        const auto* weapon = world::find_weapon_definition(tool_id);
        const auto pitch_bounds =
            weapon == nullptr ? std::array<float, 2U>{} : weapon->reload_sound_pitch;
        ++impl_->next_pitch_sequence;
        const auto pitch_draw = static_cast<std::uint32_t>(
            (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
        impl_->play(group[variant % group.size()],
                    head_relative ? SoundPosition{} : position,
                    gain,
                    head_relative,
                    false,
                    0.0F,
                    retail_default_attenuation,
                    SpatialSoundProfile::ordinary,
                    retail_sound_pitch_ratio(pitch_bounds[0U], pitch_bounds[1U], pitch_draw),
                    tool_cue_reverb_send(head_relative));
    }
}

void OpenAlFrontendAudio::play_weapon_reload_done(std::uint8_t tool_id,
                                                  std::uint8_t variant,
                                                  SoundPosition position,
                                                  float gain,
                                                  bool head_relative) {
    if (tool_id >= impl_->weapon_reload_done_groups.size()) {
        return;
    }
    const auto& group = impl_->weapon_reload_done_groups[tool_id];
    if (!group.empty()) {
        const auto* weapon = world::find_weapon_definition(tool_id);
        const auto pitch_bounds =
            weapon == nullptr ? std::array<float, 2U>{} : weapon->reload_done_sound_pitch;
        ++impl_->next_pitch_sequence;
        const auto pitch_draw = static_cast<std::uint32_t>(
            (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
        impl_->play(group[variant % group.size()],
                    head_relative ? SoundPosition{} : position,
                    gain,
                    head_relative,
                    false,
                    0.0F,
                    retail_default_attenuation,
                    SpatialSoundProfile::ordinary,
                    retail_sound_pitch_ratio(pitch_bounds[0U], pitch_bounds[1U], pitch_draw),
                    tool_cue_reverb_send(head_relative));
    }
}

void OpenAlFrontendAudio::play_weapon_cue(std::uint8_t tool_id,
                                          WeaponCue cue,
                                          std::uint8_t variant,
                                          SoundPosition position,
                                          float gain,
                                          bool head_relative,
                                          SpatialSoundProfile profile) {
    const auto* group = impl_->cue_group(tool_id, cue);
    if (group == nullptr) {
        // Retail is deliberately silent in this role for this tool.
        return;
    }
    // Character.play_sound applies the row's semitone bounds (e.g. woosh
    // +-0.4 as a swing, +-0.8 as a throw).
    const auto bounds = impl_->weapon_cue_pitches[tool_id][static_cast<std::size_t>(cue)];
    ++impl_->next_pitch_sequence;
    const auto pitch_draw = static_cast<std::uint32_t>(
        (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
        (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
    impl_->play((*group)[variant % group->size()],
                head_relative ? SoundPosition{} : position,
                gain,
                head_relative,
                false,
                0.0F,
                spatial_rolloff(profile),
                profile,
                retail_sound_pitch_ratio(bounds.minimum, bounds.maximum, pitch_draw),
                tool_cue_reverb_send(head_relative, cue));
}

bool OpenAlFrontendAudio::has_weapon_cue(std::uint8_t tool_id, WeaponCue cue) const noexcept {
    return impl_->cue_group(tool_id, cue) != nullptr;
}

LoopVoice OpenAlFrontendAudio::start_weapon_loop(std::uint8_t tool_id,
                                                 WeaponCue cue,
                                                 SoundPosition position,
                                                 float gain,
                                                 bool head_relative,
                                                 SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return invalid_loop_voice;
    }
    const auto* group = impl_->cue_group(tool_id, cue);
    if (group == nullptr) {
        return invalid_loop_voice;
    }
    const ALuint buffer = impl_->buffer_for(group->front());
    if (buffer == 0U) {
        return invalid_loop_voice;
    }
    // MediaManager.play's allocation-time HEARING_DISTANCE rejection.
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        return invalid_loop_voice;
    }
    const auto found =
        std::ranges::find_if(impl_->loop_slots, [](const auto& entry) { return !entry.active; });
    if (found == impl_->loop_slots.end()) {
        impl_->last_error = "OpenAL loop source pool is empty";
        return invalid_loop_voice;
    }
    auto* const slot = &*found;

    clear_openal_error();
    alSourceStop(slot->source);
    alSourcei(slot->source, AL_BUFFER, 0);
    alSourcei(slot->source, AL_LOOPING, AL_TRUE);
    alSourcei(slot->source, AL_SOURCE_RELATIVE, head_relative ? AL_TRUE : AL_FALSE);
    alSourcef(slot->source, AL_PITCH, 1.0F);
    alSourcef(slot->source, AL_ROLLOFF_FACTOR, head_relative ? 0.0F : spatial_rolloff(profile));
    alSourcef(slot->source,
              AL_GAIN,
              Impl::resolved_spatial_gain(gain));
    alSourcef(slot->source, AL_REFERENCE_DISTANCE, retail_reference_distance);
    const auto placed = Impl::source_position(position, head_relative);
    alSource3f(slot->source, AL_POSITION, placed.x, placed.y, placed.z);
    // Tool loops are Tool.play_sound cues: IN_WORLD (wet) for observers, the
    // HUD zone (dry) for the player holding the tool.
    impl_->apply_world_reverb(slot->source, tool_cue_reverb_send(head_relative, cue));
    alSourcei(slot->source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcePlay(slot->source);
    if (alGetError() != AL_NO_ERROR) {
        return invalid_loop_voice;
    }
    slot->active = true;
    slot->voice = impl_->next_loop_voice++;
    slot->position = position;
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    slot->profile = profile;
    slot->relative = head_relative;
    slot->pending = false;
    slot->pending_canonical.clear();
    return slot->voice;
}

LoopVoice OpenAlFrontendAudio::start_named_voice_loop(std::string_view stem,
                                                      SoundPosition position,
                                                      float gain,
                                                      bool head_relative,
                                                      SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || stem.empty() ||
        !finite_position(position)) {
        return invalid_loop_voice;
    }
    // MediaManager.play's allocation-time HEARING_DISTANCE rejection.
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        return invalid_loop_voice;
    }
    // First use decodes on a worker: the slot is reserved now and tick()
    // starts it at the latest position/gain when the upload lands.
    const auto request = impl_->request_named_buffer_async("sounds", stem);
    if (request.handle == 0U && !request.pending) {
        return invalid_loop_voice;
    }
    const auto found =
        std::ranges::find_if(impl_->loop_slots, [](const auto& entry) { return !entry.active; });
    if (found == impl_->loop_slots.end()) {
        impl_->last_error = "OpenAL loop source pool is empty";
        return invalid_loop_voice;
    }
    auto* const slot = &*found;
    slot->position = position;
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    slot->profile = profile;
    slot->relative = head_relative;
    // Presentation loops (jetpack, parachute, fuses, flights) are IN_WORLD.
    slot->reverb_send = true;
    if (request.handle == 0U) {
        slot->pending = true;
        slot->pending_canonical = request.canonical;
    } else {
        const ALuint buffer = impl_->buffer_for(request.handle);
        if (buffer == 0U || !impl_->begin_loop_playback(*slot, buffer, slot->reverb_send)) {
            return invalid_loop_voice;
        }
        slot->pending = false;
        slot->pending_canonical.clear();
    }
    slot->active = true;
    slot->voice = impl_->next_loop_voice++;
    return slot->voice;
}

void OpenAlFrontendAudio::update_weapon_loop(LoopVoice voice,
                                             SoundPosition position,
                                             float pitch,
                                             float gain) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    auto* slot = impl_->find_loop(voice);
    if (slot == nullptr) {
        return;
    }
    slot->position = position;
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    if (!slot->relative) {
        const auto placed = retail_source_position(position);
        alSource3f(slot->source, AL_POSITION, placed.x, placed.y, placed.z);
    }
    // The floor is 0.01, not 0.25: the minigun's spin loop is pitched by its
    // barrel speed as a raw playback ratio, so a 0.25 floor would clip the
    // entire lower half of the spin-up -- exactly the build-up that makes the
    // weapon sound like it is winding on. It must stay strictly positive, since
    // OpenAL rejects a pitch of zero with AL_INVALID_VALUE and silently keeps
    // whatever pitch the source had.
    alSourcef(slot->source, AL_PITCH, std::clamp(pitch, 0.01F, 4.0F));
    impl_->refresh_weapon_loop_gain(*slot);
}

void OpenAlFrontendAudio::stop_weapon_loop(LoopVoice voice,
                                           std::uint8_t tool_id,
                                           WeaponCue tail,
                                           SoundPosition position,
                                           float gain,
                                           bool head_relative,
                                           SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    if (auto* slot = impl_->find_loop(voice); slot != nullptr) {
        alSourceStop(slot->source);
        alSourcei(slot->source, AL_BUFFER, 0);
        slot->active = false;
        slot->voice = invalid_loop_voice;
        slot->position = {};
        slot->requested_gain = 0.0F;
        slot->profile = SpatialSoundProfile::ordinary;
        slot->relative = false;
        slot->pending = false;
    }
    // Retail closes the loop and immediately plays the tail as a one-shot.
    play_weapon_cue(tool_id, tail, 0U, position, gain, head_relative, profile);
}

void OpenAlFrontendAudio::stop_weapon_loop(LoopVoice voice) noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    auto* slot = impl_->find_loop(voice);
    if (slot == nullptr) {
        return;
    }
    alSourceStop(slot->source);
    alSourcei(slot->source, AL_BUFFER, 0);
    slot->active = false;
    slot->voice = invalid_loop_voice;
    slot->position = {};
    slot->requested_gain = 0.0F;
    slot->profile = SpatialSoundProfile::ordinary;
    slot->relative = false;
    slot->pending = false;
}

std::size_t OpenAlFrontendAudio::active_loop_voices() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(impl_->loop_slots, [](const auto& slot) { return slot.active; }));
}

core::TickDecision OpenAlFrontendAudio::tick(const core::TickContext& context) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        impl_->last_error = impl_->context == nullptr
                                ? "OpenAL frontend audio is not started"
                                : "OpenAL frontend audio ticked from non-owner thread";
        return core::TickDecision::stop;
    }

    clear_openal_error();
    impl_->pump_async_named_buffers();
    impl_->reclaim_finished_voices();
    if (impl_->music_requested) {
        ALint state{};
        alGetSourcei(impl_->music_source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING &&
            !impl_->play_music(impl_->music_track, 0.0F, impl_->music_fade_seconds)) {
            return core::TickDecision::continue_running;
        }
    }

    // MediaManager.handle_old_music: the outgoing track loses its own
    // fade_speed_when_finished (1/6.5, or 1/1.5 for the select bed) per second.
    if (impl_->fade_active && impl_->fade_source != 0U) {
        impl_->fade_gain -=
            static_cast<float>(std::chrono::duration<double>{context.fixed_delta}.count()) *
            impl_->fade_rate;
        if (impl_->fade_gain <= 0.0F) {
            alSourceStop(impl_->fade_source);
            alSourcei(impl_->fade_source, AL_BUFFER, 0);
            impl_->fade_active = false;
        } else {
            alSourcef(impl_->fade_source, AL_GAIN, impl_->fade_gain);
        }
    }
    if (impl_->ambience_active) {
        // GameScene.update: a global bed is scaled by 1 - 0.8 * ducking.
        const float ducked = ducked_ambience_volume(impl_->ambience_target_gain,
                                                    impl_->environment.ambience_ducking);
        if (std::fabs(ducked - impl_->ambience_gain) > 1.0e-5F) {
            impl_->ambience_gain = ducked;
            alSourcef(impl_->ambience_source, AL_GAIN, impl_->ambience_gain);
        }
    }

    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "OpenAL frontend maintenance failed: " + openal_error_text(error);
    }
    return core::TickDecision::continue_running;
}

void OpenAlFrontendAudio::stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }

    ALCcontext* displaced_context{};
    if (impl_->context != nullptr && alcGetCurrentContext() != impl_->context) {
        displaced_context = alcGetCurrentContext();
        static_cast<void>(alcMakeContextCurrent(impl_->context));
    }

    if (impl_->context != nullptr) {
        impl_->stop_music(true);
        for (auto& voice : impl_->voices) {
            if (voice.source != 0U) {
                alSourceStop(voice.source);
                alSourcei(voice.source, AL_BUFFER, 0);
                alDeleteSources(1, &voice.source);
                voice.source = 0U;
                voice.active = false;
                voice.protected_voice = false;
            }
        }
        if (impl_->music_source != 0U) {
            alDeleteSources(1, &impl_->music_source);
            impl_->music_source = 0U;
        }
        if (impl_->fade_source != 0U) {
            alDeleteSources(1, &impl_->fade_source);
            impl_->fade_source = 0U;
            impl_->fade_active = false;
        }
        if (impl_->ambience_source != 0U) {
            impl_->stop_ambience();
            alDeleteSources(1, &impl_->ambience_source);
            impl_->ambience_source = 0U;
        }
        for (auto& slot : impl_->loop_slots) {
            if (slot.source != 0U) {
                alSourceStop(slot.source);
                alSourcei(slot.source, AL_BUFFER, 0);
                alDeleteSources(1, &slot.source);
                slot.source = 0U;
            }
            slot.active = false;
            slot.voice = invalid_loop_voice;
        }
        for (auto& slot : impl_->server_loop_slots) {
            if (slot.source != 0U) {
                impl_->stop_server_loop(slot);
                alDeleteSources(1, &slot.source);
                slot.source = 0U;
            }
        }
        // EFX objects belong to the OpenAL context and must be destroyed before
        // the context. Resolver installation/removal must never touch them.
        impl_->release_world_reverb();
        for (auto& buffer : impl_->buffers) {
            if (buffer.id != 0U) {
                alDeleteBuffers(1, &buffer.id);
                buffer.id = 0U;
            }
        }
    }

    impl_->voices.clear();
    impl_->buffers.clear();
    impl_->next_sequence = 0U;
    impl_->next_pitch_sequence = 0U;
    impl_->listener_position = {};
    impl_->music_requested = false;
    impl_->fade_active = false;
    impl_->music_fade_seconds = retail_music_fade_seconds;
    impl_->environment = {};
    impl_->applied_reverb_gain = 0.0F;
    impl_->applied_reverb_decay = 1.0F;
    impl_->weapon_cue_pitches = {};

    // Every buffer these name has just been deleted. Leaving them populated
    // makes a restart hand out dangling handles and reuse stale ids, so the
    // whole optional-sound bookkeeping resets with the buffers it describes.
    impl_->optional_sound_cache.clear();
    for (auto& cache : impl_->named_cache) {
        cache.clear();
    }
    impl_->evictable_buffers.clear();
    impl_->pending_one_shots.clear();
    impl_->pending_ambience.reset();
    // std::future destructors join the decode workers.
    impl_->pending_named_buffers.clear();
    impl_->pending_server_loops.clear();
    impl_->pending_music.reset();
    impl_->next_optional_handle = 1'000U;
    impl_->next_loop_voice = 1U;
    impl_->cosmetic_fire_groups.clear();
    impl_->cosmetic_cues.clear();
    for (auto& group : impl_->weapon_shoot_groups)
        group.clear();
    for (auto& group : impl_->weapon_reload_groups)
        group.clear();
    for (auto& group : impl_->weapon_reload_done_groups)
        group.clear();
    for (auto& set : impl_->weapon_sound_sets) {
        for (auto& group : set)
            group.clear();
    }

    if (impl_->context != nullptr) {
        if (alcGetCurrentContext() == impl_->context) {
            static_cast<void>(alcMakeContextCurrent(nullptr));
        }
        alcDestroyContext(impl_->context);
        impl_->context = nullptr;
    }
    if (impl_->device != nullptr) {
        static_cast<void>(alcCloseDevice(impl_->device));
        impl_->device = nullptr;
    }
    if (displaced_context != nullptr) {
        static_cast<void>(alcMakeContextCurrent(displaced_context));
    }
    impl_->owner_thread = {};
    impl_->playback_device_name.clear();
}

void OpenAlFrontendAudio::set_listener(SoundPosition position) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        impl_->last_error = impl_->context == nullptr
                                ? "OpenAL frontend audio is not started"
                                : "OpenAL listener changed from non-owner thread";
        return;
    }
    if (!finite_position(position)) {
        impl_->last_error = "invalid OpenAL listener position";
        return;
    }

    clear_openal_error();
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    impl_->listener_position = position;
    // Retail never re-culls a started loop, so moving the listener changes
    // only OpenAL's own distance attenuation.
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot change OpenAL listener: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::set_listener_pose(SoundPosition position,
                                            const std::array<float, 3U>& forward,
                                            const std::array<float, 3U>& up) {
    const auto finite_vector = [](const std::array<float, 3U>& value) {
        return std::ranges::all_of(value, [](float component) { return std::isfinite(component); });
    };
    const auto length_squared = [](const std::array<float, 3U>& value) {
        return value[0U] * value[0U] + value[1U] * value[1U] + value[2U] * value[2U];
    };
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        impl_->last_error = impl_->context == nullptr
                                ? "OpenAL frontend audio is not started"
                                : "OpenAL listener changed from non-owner thread";
        return;
    }
    if (!finite_position(position) || !finite_vector(forward) || !finite_vector(up) ||
        length_squared(forward) < 0.5F || length_squared(up) < 0.5F) {
        impl_->last_error = "invalid OpenAL listener pose";
        return;
    }
    const std::array<ALfloat, 6U> orientation{
        forward[0U], forward[1U], forward[2U], up[0U], up[1U], up[2U]};
    clear_openal_error();
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    alListenerfv(AL_ORIENTATION, orientation.data());
    impl_->listener_position = position;
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot change OpenAL listener pose: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::apply_environment_audio(const EnvironmentAudioState& state) {
    const auto finite_or = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    impl_->environment.reverb_size = finite_or(state.reverb_size, 1.0F);
    impl_->environment.reverb_amount = std::clamp(finite_or(state.reverb_amount, 0.0F), 0.0F, 1.0F);
    impl_->environment.ambience_ducking =
        std::clamp(finite_or(state.ambience_ducking, 0.0F), 0.0F, 1.0F);
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    clear_openal_error();
    impl_->update_world_reverb();
    clear_openal_error();
}

EnvironmentAudioState OpenAlFrontendAudio::environment_audio() const noexcept {
    return impl_->environment;
}

void OpenAlFrontendAudio::play_one_shot(SoundHandle sound, SoundPosition position, float gain) {
    impl_->play(sound, position, gain, false);
}

void OpenAlFrontendAudio::play_pitched_one_shot(SoundHandle sound,
                                                SoundPosition position,
                                                float gain,
                                                float pitch,
                                                bool head_relative) {
    impl_->play(sound,
                head_relative ? SoundPosition{} : position,
                gain,
                head_relative,
                false,
                0.0F,
                retail_default_attenuation,
                SpatialSoundProfile::ordinary,
                pitch);
}

void OpenAlFrontendAudio::play_head_relative_one_shot(SoundHandle sound, float gain) {
    impl_->play(sound, SoundPosition{}, gain, true);
}

bool OpenAlFrontendAudio::play_named_one_shot(std::string_view stem,
                                              SoundPosition position,
                                              float gain,
                                              bool head_relative,
                                              float start_offset,
                                              float attenuation,
                                              bool protect_voice,
                                              float pitch,
                                              bool reverb_send) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || stem.empty()) {
        return false;
    }
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        return true;
    }
    // A cold sample decodes on a worker; the cue plays on the tick its
    // upload lands instead of stalling the game thread on Vorbis decode.
    Impl::PendingOneShot shot;
    shot.position = head_relative ? SoundPosition{} : position;
    shot.gain = gain;
    shot.relative = head_relative;
    shot.protect = protect_voice;
    shot.start_offset = start_offset;
    shot.attenuation = attenuation;
    shot.pitch = pitch;
    shot.reverb_send = reverb_send;
    return impl_->play_named_or_defer("sounds", stem, std::move(shot));
}

bool OpenAlFrontendAudio::play_named_voice_line(std::uint16_t speaker,
                                                std::string_view stem,
                                                SoundPosition position,
                                                float gain,
                                                bool head_relative) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || stem.empty()) {
        return false;
    }
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        // Character.play_sound returned None: the current line keeps playing.
        return true;
    }
    Impl::PendingOneShot shot;
    shot.position = head_relative ? SoundPosition{} : position;
    shot.gain = gain;
    shot.relative = head_relative;
    shot.protect = true;
    shot.attenuation = retail_default_attenuation;
    shot.speaker = speaker;
    return impl_->play_named_or_defer("sounds", stem, std::move(shot));
}

bool OpenAlFrontendAudio::play_named_music(std::string_view stem, float start_offset) {
    return impl_->play_named_music(stem, start_offset, retail_music_fade_seconds);
}

bool OpenAlFrontendAudio::music_fading() const noexcept {
    return impl_->fade_active;
}

bool OpenAlFrontendAudio::is_playing_named_music(std::string_view stem) const noexcept {
    return (impl_->music_requested || impl_->pending_music.has_value()) &&
           !impl_->named_music_stem.empty() && impl_->named_music_stem == stem;
}

bool OpenAlFrontendAudio::preload_named_ambience(std::string_view stem) {
    // Worker decode as well: a 10-20 MB bed decoded here used to stall the
    // loading gate's main thread. play_named_ambience picks up a pending bed.
    return preload_named_ambience_async(stem);
}

bool OpenAlFrontendAudio::preload_named_ambience_async(std::string_view stem) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    const auto request = impl_->request_named_buffer_async("ambients", stem);
    return request.handle != 0U || request.pending;
}

bool OpenAlFrontendAudio::play_named_ambience(std::string_view stem,
                                              float volume,
                                              float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    const auto request = impl_->request_named_buffer_async("ambients", stem);
    if (request.handle != 0U) {
        return impl_->play_ambience(request.handle, volume, start_offset);
    }
    if (!request.pending) {
        return false;
    }
    // Last request wins; the bed starts on the tick its upload lands.
    impl_->pending_ambience =
        Impl::PendingAmbienceRequest{request.canonical, volume, start_offset};
    return true;
}

bool OpenAlFrontendAudio::play_named_ambient_one_shot(std::string_view stem,
                                                      SoundPosition position,
                                                      float volume,
                                                      bool head_relative,
                                                      float attenuation,
                                                      float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        return true;
    }
    // process_packet_play_ambient_sound uses HUD_AUDIO_ZONE: dry.
    Impl::PendingOneShot shot;
    shot.position = head_relative ? SoundPosition{} : position;
    shot.gain = volume;
    shot.relative = head_relative;
    shot.protect = true;
    shot.start_offset = start_offset;
    shot.attenuation = attenuation;
    shot.reverb_send = false;
    return impl_->play_named_or_defer("ambients", stem, std::move(shot));
}

bool OpenAlFrontendAudio::start_named_loop(std::uint8_t loop_id,
                                           std::string_view stem,
                                           bool ambient_asset,
                                           SoundPosition position,
                                           float gain,
                                           bool head_relative,
                                           float attenuation,
                                           float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    // Both trees decode cold samples on a worker; the loop starts when the
    // upload lands (pump_async_named_buffers).
    const auto request =
        impl_->request_named_buffer_async(ambient_asset ? "ambients" : "sounds", stem);
    if (request.handle != 0U) {
        impl_->pending_server_loops.erase(loop_id);
        return impl_->start_server_loop(
            loop_id, request.handle, position, gain, head_relative, attenuation, start_offset);
    }
    if (request.pending) {
        impl_->pending_server_loops[loop_id] = Impl::PendingServerLoopRequest{
            request.canonical, position, gain, head_relative, attenuation, start_offset};
        return true;
    }
    return false;
}

void OpenAlFrontendAudio::update_named_loop(std::uint8_t loop_id, SoundPosition position) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || !finite_position(position)) {
        return;
    }
    auto* const slot = impl_->find_server_loop(loop_id);
    if (slot == nullptr || slot->relative) {
        return;
    }
    clear_openal_error();
    slot->position = position;
    // GameSound.set_position applies the same +0.5 offset on every update.
    const auto placed = retail_source_position(position);
    alSource3f(slot->source, AL_POSITION, placed.x, placed.y, placed.z);
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot reposition Protocol 168 loop: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::set_named_loop_mix(std::uint8_t loop_id,
                                             float gain,
                                             bool non_positional) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || !std::isfinite(gain)) {
        return;
    }
    auto* const slot = impl_->find_server_loop(loop_id);
    if (slot == nullptr) {
        return;
    }
    clear_openal_error();
    if (non_positional && !slot->relative) {
        slot->relative = true;
        alSourcei(slot->source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef(slot->source, AL_ROLLOFF_FACTOR, 0.0F);
        alSource3f(slot->source, AL_POSITION, 0.0F, 0.0F, 0.0F);
    }
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    impl_->refresh_server_loop_gain(*slot);
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot remix Protocol 168 loop: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::stop_named_loop(std::uint8_t loop_id) noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    impl_->pending_server_loops.erase(loop_id);
    if (auto* const slot = impl_->find_server_loop(loop_id); slot != nullptr) {
        impl_->stop_server_loop(*slot);
    }
}

void OpenAlFrontendAudio::stop_named_loops() noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    impl_->pending_server_loops.clear();
    for (auto& slot : impl_->server_loop_slots) {
        if (slot.active) {
            impl_->stop_server_loop(slot);
        }
    }
}

void OpenAlFrontendAudio::stop_all() noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }

    impl_->stop_music(true);
    impl_->stop_ambience();
    stop_named_loops();
    impl_->pending_one_shots.clear();
    for (auto& slot : impl_->loop_slots) {
        if (slot.active) {
            alSourceStop(slot.source);
            alSourcei(slot.source, AL_BUFFER, 0);
            slot.active = false;
            slot.voice = invalid_loop_voice;
        }
    }
    for (auto& voice : impl_->voices) {
        alSourceStop(voice.source);
        alSourcei(voice.source, AL_BUFFER, 0);
        voice.active = false;
        voice.protected_voice = false;
    }
}

void OpenAlFrontendAudio::play_menu_confirm() {
    impl_->play_hud(menu_confirm_sound, impl_->config.cue_gain);
}

void OpenAlFrontendAudio::play_menu_back() {
    impl_->play_hud(menu_back_sound, impl_->config.cue_gain);
}

void OpenAlFrontendAudio::play_menu_scroll() {
    impl_->play_hud(menu_scroll_sound, impl_->config.cue_gain);
}

bool OpenAlFrontendAudio::play_secondary_menu_music() {
    // selectClass/selectTeam/selectUGC start SECONDARY_MENU_MUSIC1 with
    // fade_speed_when_finished = 1/SECONDARY_MUSIC_BED_FADE_TIME (1.5 s).
    return impl_->play_named_music(
        "secondary_menu_bed_001", 0.0F, retail_secondary_bed_fade_seconds);
}

void OpenAlFrontendAudio::play_zoom_toggle(bool enabled) {
    impl_->play(enabled ? zoom_in_sound : zoom_out_sound, {}, impl_->config.cue_gain, true, true);
}

SoundHandle OpenAlFrontendAudio::preload_skin_sound(const std::filesystem::path& path) {
    if(!impl_->context||!impl_->on_owner_thread()||path.extension()!=".ogg")return 0U;
    const auto base=std::filesystem::weakly_canonical(impl_->config.asset_root.parent_path()/"client/cosmetics");
    const auto resolved=std::filesystem::weakly_canonical(path);
    const auto relative=resolved.lexically_relative(base);
    if(relative.empty()||*relative.begin()=="..")return 0U;
    const auto key=path_utf8(resolved);
    if(const auto it=impl_->optional_sound_cache.find(key);it!=impl_->optional_sound_cache.end())return it->second;
    const auto handle=impl_->next_optional_handle++;
    if(!impl_->load_buffer(handle,resolved))return 0U;
    impl_->optional_sound_cache.emplace(key,handle);return handle;
}
void OpenAlFrontendAudio::play_skin_sound(SoundHandle sound,float gain,float pitch) {
    impl_->play(sound,{},gain,true,false,0.F,0.F,SpatialSoundProfile::ordinary,pitch,false);
}

void OpenAlFrontendAudio::play_respawn_countdown_beep(bool final_beat) {
    // Character.media.play('beep1'/'beep2') is a protected 2D HUD-zone cue.
    impl_->play(final_beat ? respawn_beep1_sound : respawn_beep2_sound,
                {}, impl_->config.cue_gain, true, true, 0.0F, retail_default_attenuation,
                SpatialSoundProfile::ordinary, 1.0F, false);
}

void OpenAlFrontendAudio::play_tutorial_appear() {
    impl_->play_hud(tutorial_appear_sound, impl_->config.cue_gain);
}

void OpenAlFrontendAudio::play_tutorial_disappear() {
    impl_->play_hud(tutorial_disappear_sound, impl_->config.cue_gain);
}

bool OpenAlFrontendAudio::set_master_volume(float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
        impl_->last_error = "master volume must be finite and within 0..1";
        return false;
    }
    if (impl_->context != nullptr && !impl_->on_owner_thread()) {
        impl_->last_error = "OpenAL master volume changed from a non-owner thread";
        return false;
    }

    if (impl_->context != nullptr) {
        clear_openal_error();
        alListenerf(AL_GAIN, volume);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            impl_->last_error = "cannot apply OpenAL master volume: " + openal_error_text(error);
            return false;
        }
    }
    impl_->master_volume = volume;
    impl_->last_error.clear();
    return true;
}

bool OpenAlFrontendAudio::set_music_volume(float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
        impl_->last_error = "music volume must be finite and within 0..1";
        return false;
    }
    if (impl_->context != nullptr && !impl_->on_owner_thread()) {
        impl_->last_error = "OpenAL music volume changed from a non-owner thread";
        return false;
    }

    if (impl_->context != nullptr && impl_->music_source != 0U) {
        clear_openal_error();
        alSourcef(impl_->music_source, AL_GAIN, volume);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            impl_->last_error = "cannot apply OpenAL music volume: " + openal_error_text(error);
            return false;
        }
    }
    impl_->config.music_gain = volume;
    impl_->last_error.clear();
    return true;
}

float OpenAlFrontendAudio::master_volume() const noexcept {
    return impl_->master_volume;
}

float OpenAlFrontendAudio::music_volume() const noexcept {
    return impl_->config.music_gain;
}

bool OpenAlFrontendAudio::start_menu_music() {
    impl_->pending_music.reset();
    impl_->named_music_stem.clear();
    return impl_->play_music(main_menu_music);
}

void OpenAlFrontendAudio::stop_menu_music() noexcept {
    if (impl_->context != nullptr && impl_->on_owner_thread()) {
        impl_->pending_music.reset();
        impl_->stop_music();
    }
}

bool OpenAlFrontendAudio::play_tutorial_music() {
    impl_->pending_music.reset();
    impl_->named_music_stem.clear();
    return impl_->play_music(tutorial_music);
}

bool OpenAlFrontendAudio::play_training_ambience(float volume) {
    return impl_->play_ambience(training_ambience, volume);
}

void OpenAlFrontendAudio::stop_ambience() noexcept {
    if (impl_->context != nullptr && impl_->on_owner_thread()) {
        impl_->stop_ambience();
    }
}

bool OpenAlFrontendAudio::is_started() const noexcept {
    return impl_->context != nullptr;
}

std::string_view OpenAlFrontendAudio::playback_device() const noexcept {
    return impl_->playback_device_name;
}

std::size_t OpenAlFrontendAudio::active_one_shot_voices() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(impl_->voices.begin(), impl_->voices.end(), [](const Impl::Voice& voice) {
            return voice.active;
        }));
}

std::size_t OpenAlFrontendAudio::resident_music_ambience_bytes() const noexcept {
    return impl_->evictable_bytes();
}

std::size_t OpenAlFrontendAudio::pending_named_decodes() const noexcept {
    return impl_->pending_named_buffers.size();
}

std::string_view OpenAlFrontendAudio::last_error() const noexcept {
    return impl_->last_error;
}

} // namespace battlespades::audio
